// =================================================================================================
//  ImageSearchDLL - Ultra-Fast Image Search 
//  Author: Dao Van Trong - TRONG.PRO
//  Architecture: C++17 (Windows 7 SP1)
//  Licensed under the MIT License. See LICENSE file for details.
// =================================================================================================

#pragma managed(push, off)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <algorithm>
#include <thread>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <string_view>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cwctype>
#include <cmath>
#include <cstring>
#include <deque>

#ifdef _WIN64
#include <immintrin.h>
#else
#include <emmintrin.h>
#endif
#include <intrin.h>

#ifdef _MSC_VER
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#endif

#define MAX_MATCHES 1024
#define MAX_CACHED_BITMAPS 100
#define MAX_CACHED_LOCATIONS 100
#define CACHE_MISS_THRESHOLD 3
#define MUTEX_RETRY_COUNT 3
#define MUTEX_RETRY_BASE_MS 100
#define MAX_RESULT_STRING_LENGTH 262144

static std::atomic<int> g_pixel_pool_size{ 50 };

using namespace Gdiplus;

std::wstring FormatFloat(float value, int precision = 2) {
	std::wstringstream ss;
	ss << std::fixed << std::setprecision(precision) << value;
	std::wstring result = ss.str();
	// Remove trailing zeros and redundant decimal point
	result.erase(result.find_last_not_of(L"0") + 1);
	if (result.back() == L'.') {
		result.pop_back();
	}
	return result;
}

int CalculateOptimalPoolSize() {
	MEMORYSTATUSEX memStatus;
	memStatus.dwLength = sizeof(memStatus);
	if (GlobalMemoryStatusEx(&memStatus)) {
		DWORDLONG totalRAM_GB = memStatus.ullTotalPhys / (1024ULL * 1024ULL * 1024ULL);
		int poolSize = static_cast<int>(std::min(100ULL, totalRAM_GB * 5));
		return std::max(50, poolSize);
	}
	return 50;
}

inline int ComputeAlphaThreshold(bool transparent_enabled, int tolerance) {
	if (!transparent_enabled) return 0;
	if (tolerance <= 0) return 255;
	int thresh = 255 - (tolerance * 255 / 255);
	return std::clamp(thresh, 0, 255);
}

#ifdef _MSC_VER
#define SIMD_ALIGNMENT 64
inline void* AlignedAlloc(size_t size, size_t alignment) {
	return _aligned_malloc(size, alignment);
}
inline void AlignedFree(void* ptr) {
	_aligned_free(ptr);
}
#else
#define SIMD_ALIGNMENT 64
inline void* AlignedAlloc(size_t size, size_t alignment) {
	void* ptr = nullptr;
	if (posix_memalign(&ptr, alignment, size) != 0) {
		return nullptr;
	}
	return ptr;
}
inline void AlignedFree(void* ptr) {
	free(ptr);
}
#endif

template<typename T>
struct AlignedAllocator {
	using value_type = T;

	AlignedAllocator() = default;
	template<typename U>
	AlignedAllocator(const AlignedAllocator<U>&) {}

	T* allocate(size_t n) {
		void* ptr = AlignedAlloc(n * sizeof(T), SIMD_ALIGNMENT);
		if (!ptr) {
			return nullptr;
		}
		return static_cast<T*>(ptr);
	}

	void deallocate(T* ptr, size_t) {
		AlignedFree(ptr);
	}

	template<typename U>
	bool operator==(const AlignedAllocator<U>&) const { return true; }
	template<typename U>
	bool operator!=(const AlignedAllocator<U>&) const { return false; }
};

bool WaitForMutexWithRetry(HANDLE hMutex, int retryCount = MUTEX_RETRY_COUNT) {
	for (int attempt = 0; attempt < retryCount; ++attempt) {
		DWORD timeout = MUTEX_RETRY_BASE_MS * (1 << attempt);
		DWORD result = WaitForSingleObject(hMutex, timeout);

		if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
			return true;
		}
	}
	return false;
}

class ScopedMutex {
public:
	ScopedMutex(HANDLE hMutex) : m_hMutex(hMutex), m_locked(false) {
		if (m_hMutex) {
			m_locked = WaitForMutexWithRetry(m_hMutex);
		}
	}

	~ScopedMutex() {
		if (m_locked && m_hMutex) {
			ReleaseMutex(m_hMutex);
		}
	}

	bool IsLocked() const {
		return m_locked;
	}

	ScopedMutex(const ScopedMutex&) = delete;
	ScopedMutex& operator=(const ScopedMutex&) = delete;

private:
	HANDLE m_hMutex;
	bool m_locked;
};

ULONG_PTR g_gdiplusToken = 0;
std::once_flag g_gdiplus_init_flag;

void InitializeGdiplus() {
	std::call_once(g_gdiplus_init_flag, []() {
		GdiplusStartupInput gdiplusStartupInput;
		GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
		});
}

#ifdef _WIN64
std::atomic<bool> g_is_avx2_supported{ false };
std::atomic<bool> g_is_avx512_supported{ false };
#else
std::atomic<bool> g_is_sse2_supported{ false };
#endif

std::once_flag g_feature_detection_flag;

uint64_t SafeGetXCR0() {
	uint64_t xcr0 = 0;
#ifdef _MSC_VER
	__try {
#if defined(_XCR_XFEATURE_ENABLED_MASK)
		xcr0 = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
#else
		xcr0 = _xgetbv(0);
#endif
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		xcr0 = 0;
	}
#elif defined(__GNUC__) || defined(__clang__)
	try {
		unsigned int eax = 0, edx = 0;
		__asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0) : );
		xcr0 = (static_cast<uint64_t>(edx) << 32) | eax;
	}
	catch (...) {
		xcr0 = 0;
	}
#endif
	return xcr0;
}

void DetectCpuFeatures() {
	int cpuInfo[4] = { 0 };
	__cpuid(cpuInfo, 0);
	int maxLeaf = cpuInfo[0];

	if (maxLeaf < 1) {
#ifdef _WIN64
		g_is_avx2_supported.store(false, std::memory_order_relaxed);
		g_is_avx512_supported.store(false, std::memory_order_relaxed);
#else
		g_is_sse2_supported.store(false, std::memory_order_relaxed);
#endif
		return;
	}

	int regs1[4] = { 0 };
	__cpuid(regs1, 1);

#ifdef _WIN64
	bool osxsave = (regs1[2] & (1 << 27)) != 0;
	bool avx_os_support = false;

	if (osxsave) {
		uint64_t xcr0 = SafeGetXCR0();
		if ((xcr0 & 0x6ULL) == 0x6ULL) {
			avx_os_support = true;
		}
	}

	bool has_avx2 = false;
	bool has_avx512 = false;

	if (avx_os_support && maxLeaf >= 7) {
		int regs7[4] = { 0 };
		__cpuidex(regs7, 7, 0);
		has_avx2 = (regs7[1] & (1 << 5)) != 0;
		const int avx512_mask = (1 << 16) | (1 << 17) | (1 << 21) | (1 << 30) | (1 << 31);
		has_avx512 = (regs7[1] & avx512_mask) == avx512_mask;
	}

	g_is_avx2_supported.store(has_avx2, std::memory_order_relaxed);
	g_is_avx512_supported.store(has_avx512, std::memory_order_relaxed);
#else
	bool has_sse2 = (regs1[3] & (1 << 26)) != 0;
	g_is_sse2_supported.store(has_sse2, std::memory_order_relaxed);
#endif
}

void DetectFeatures() {
	DetectCpuFeatures();
}

enum class ErrorCode : int {
	Success = 0,
	InvalidPath = -1,
	FailedToLoadImage = -2,
	FailedToGetScreenDC = -3,
	InvalidSearchRegion = -4,
	InvalidParameters = -5,
	InvalidSourceBitmap = -6,
	InvalidTargetBitmap = -7,
	ResultTooLarge = -9,
	InvalidMonitor = -10
};

const wchar_t* GetErrorMessage(ErrorCode code) {
	switch (code) {
	case ErrorCode::InvalidPath: return L"Invalid path or image format";
	case ErrorCode::FailedToLoadImage: return L"Failed to load image from file";
	case ErrorCode::FailedToGetScreenDC: return L"Failed to get screen device context or get valid Source pixels";
	case ErrorCode::InvalidSearchRegion: return L"Invalid search region specified";
	case ErrorCode::InvalidParameters: return L"Invalid parameters provided";
	case ErrorCode::InvalidSourceBitmap: return L"Invalid Source (source) bitmap";
	case ErrorCode::InvalidTargetBitmap: return L"Invalid Target (target) bitmap";
	case ErrorCode::ResultTooLarge: return L"Result String Too Large";
	case ErrorCode::InvalidMonitor: return L"Invalid monitor index";
	default: return L"Unknown error";
	}
}

inline std::wstring FormatError(ErrorCode code) {
	return L"{" + std::to_wstring(static_cast<int>(code)) + L"}[]<" + GetErrorMessage(code) + L">";
}

struct PixelBufferPool;
extern PixelBufferPool g_pixel_pool;

struct PixelBuffer {
	std::vector<COLORREF> pixels;
	int width = 0;
	int height = 0;
	bool has_alpha = false;
	bool owns_memory = true;

	bool IsValid() const {
		return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width * height);
	}

	~PixelBuffer();

	PixelBuffer() = default;
	PixelBuffer(const PixelBuffer&) = delete;
	PixelBuffer& operator=(const PixelBuffer&) = delete;
	PixelBuffer(PixelBuffer&& other) noexcept;
	PixelBuffer& operator=(PixelBuffer&& other) noexcept;
};

struct MatchResult {
	int x, y, w, h;
	float scale = 1.0f;
	std::wstring source_file;

	MatchResult(int _x = 0, int _y = 0, int _w = 0, int _h = 0, float _scale = 1.0f, const std::wstring& _source = L"")
		: x(_x), y(_y), w(_w), h(_h), scale(_scale), source_file(_source) {
	}
};

struct CacheEntry {
	POINT position = { 0, 0 };
	int miss_count = 0;
	std::chrono::steady_clock::time_point last_used = std::chrono::steady_clock::now();
};

// ============================================================================
// CACHE SYSTEM - Global Variables
// ============================================================================
// Description:
//   Two-tier caching system for image search results:
//   1. In-memory LRU cache (fast, volatile)
//   2. Persistent disk cache (survives DLL reload, slower)
//
// Cache Key Format:
//   "normalized_path|tolerance|transparent|scale"
//   Example: "c:\images\icon.png|10|1|1.0"
//
// Cache Validation:
//   - Each cache entry tracks miss count
//   - After 3 consecutive misses, entry is invalidated and removed
//   - Prevents stale cache from causing false negatives
// ============================================================================
std::deque<std::pair<std::wstring, CacheEntry>> g_location_cache_lru;  // LRU queue: most recent first
std::unordered_map<std::wstring, size_t> g_location_cache_index;       // Fast lookup: key -> index in LRU
std::shared_mutex g_cache_mutex;                                       // Thread-safe access to cache
std::wstring g_cache_base_dir;                                        // Base directory for disk cache files
HANDLE g_hCacheFileMutex = nullptr;                                   // System-wide mutex for file cache

// ============================================================================
// HELPER: _RebuildCacheIndex
// ============================================================================
// Description:
//   Rebuilds the hash map index after LRU queue modifications.
//   Called after insertions, deletions, or reordering of cache entries.
// ============================================================================
void _RebuildCacheIndex() {
	g_location_cache_index.clear();
	for (size_t i = 0; i < g_location_cache_lru.size(); ++i) {
		g_location_cache_index[g_location_cache_lru[i].first] = i;
	}
}

// ============================================================================
// HELPER: GetCacheBaseDir
// ============================================================================
// Description:
//   Returns the base directory for persistent disk cache files.
//   Uses system temp directory and creates it if needed.
// ============================================================================
std::wstring GetCacheBaseDir() {
	if (!g_cache_base_dir.empty()) return g_cache_base_dir;

	wchar_t temp_path[MAX_PATH];
	DWORD path_len = GetTempPathW(MAX_PATH, temp_path);
	if (path_len > 0 && path_len < MAX_PATH) {
		g_cache_base_dir = temp_path;
		try {
			std::filesystem::create_directories(g_cache_base_dir);
		}
		catch (...) {}
		return g_cache_base_dir;
	}
	return L"";
}

// ============================================================================
// HELPER: GetNormalizedPathKey
// ============================================================================
// Description:
//   Normalizes file paths for consistent cache key generation.
//   Converts to canonical form and lowercase to handle path variations:
//   - "C:\Images\Icon.png" and "c:\images\icon.png" -> same key
//   - Relative vs absolute paths -> same key if same file
//   - Forward vs backslashes -> same key
//
// Returns:
//   Normalized lowercase canonical path, or lowercase original on error
// ============================================================================
std::wstring GetNormalizedPathKey(const std::wstring& path_str) {
	if (path_str.empty()) return L"";
	try {
		// Use weakly_canonical to handle non-existent paths gracefully
		std::wstring canonical_path = std::filesystem::weakly_canonical(path_str).wstring();
		std::transform(canonical_path.begin(), canonical_path.end(), canonical_path.begin(),
			[](wchar_t c) { return std::towlower(c); });
		return canonical_path;
	}
	catch (...) {
		// Fallback: just lowercase the original path
		std::wstring lower_path = path_str;
		std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
			[](wchar_t c) { return std::towlower(c); });
		return lower_path;
	}
}

// ============================================================================
// HELPER: GenerateCacheKey
// ============================================================================
// Description:
//   Generates a unique cache key from search parameters.
//   Two searches with identical parameters will produce identical keys,
//   ensuring cache hits for repeated searches.
//
// Format:
//   "normalized_primary_path|normalized_secondary_path|tolerance|transparent|scale"
//
// Example:
//   GenerateCacheKey("C:\screen.png", "icon.png", 10, true, 1.0)
//   -> "c:\screen.png|c:\icon.png|10|1|1.0"
// ============================================================================
std::wstring GenerateCacheKey(const std::wstring& primary_path, const std::wstring& secondary_path = L"",
	int tolerance = 0, bool transparent = false, float scale = 1.0f) {
	std::wstring normalized_primary = GetNormalizedPathKey(primary_path);
	std::wstringstream ss;
	ss << normalized_primary;
	if (!secondary_path.empty()) {
		std::wstring normalized_secondary = GetNormalizedPathKey(secondary_path);
		ss << L"|" << normalized_secondary;
	}
	ss << L"|" << tolerance << L"|" << transparent << L"|" << std::fixed << std::setprecision(1) << scale;
	return ss.str();
}

std::wstring GetCacheFileForImage(const std::wstring& cache_key) {
	std::size_t hasher = std::hash<std::wstring>{}(cache_key);
	std::wstringstream ss;
	ss << L"~CACHE_IMGSEARCH_V2_" << std::hex << std::uppercase << hasher << L".dat";
	std::filesystem::path file_path = std::filesystem::path(GetCacheBaseDir()) / ss.str();
	return file_path.wstring();
}

void LoadCacheForImage(const std::wstring& cache_key) {
	if (cache_key.empty()) return;

	if (!g_hCacheFileMutex) return;

	ScopedMutex file_lock(g_hCacheFileMutex);
	if (!file_lock.IsLocked()) return;

	try {
		std::wstring cache_file_path = GetCacheFileForImage(cache_key);
		std::wifstream cache_file(cache_file_path);
		if (cache_file.is_open()) {
			std::wstring line;
			std::getline(cache_file, line);
			size_t pos = line.find(L'|');
			if (pos != std::wstring::npos) {
				std::wstring x_str = line.substr(0, pos);
				std::wstring y_str = line.substr(pos + 1);

				try {
					int x = std::stoi(x_str);
					int y = std::stoi(y_str);

					if (x >= -10000 && x <= 50000 && y >= -10000 && y <= 50000) {
						CacheEntry entry;
						entry.position = { x, y };
						entry.miss_count = 0;
						entry.last_used = std::chrono::steady_clock::now();

						std::unique_lock lock(g_cache_mutex);
						g_location_cache_lru.push_front({ cache_key, entry });

						while (g_location_cache_lru.size() > MAX_CACHED_LOCATIONS) {
							g_location_cache_lru.pop_back();
						}

						_RebuildCacheIndex();
					}
				}
				catch (const std::exception&) {
				}
			}
		}
	}
	catch (...) {
	}
}

void SaveCacheForImage(const std::wstring& cache_key, POINT pos) {
	if (cache_key.empty()) return;

	if (!g_hCacheFileMutex) return;

	ScopedMutex file_lock(g_hCacheFileMutex);
	if (!file_lock.IsLocked()) return;

	try {
		std::wstring cache_file_path = GetCacheFileForImage(cache_key);
		std::filesystem::create_directories(std::filesystem::path(cache_file_path).parent_path());
		std::wofstream cache_file(cache_file_path, std::ios::trunc);
		if (cache_file.is_open()) {
			cache_file << pos.x << L"|" << pos.y;
		}
	}
	catch (...) {}
}

void RemoveFromCache(const std::wstring& cache_key) {
	if (cache_key.empty()) return;

	{
		std::unique_lock lock(g_cache_mutex);
		auto it = g_location_cache_index.find(cache_key);
		if (it != g_location_cache_index.end()) {
			size_t idx = it->second;
			if (idx < g_location_cache_lru.size()) {
				g_location_cache_lru.erase(g_location_cache_lru.begin() + idx);
				_RebuildCacheIndex();
			}
		}
	}

	try {
		std::wstring cache_file_path = GetCacheFileForImage(cache_key);
		std::filesystem::remove(cache_file_path);
	}
	catch (...) {}
}

std::optional<CacheEntry> GetCachedLocation(const std::wstring& cache_key) {
	std::unique_lock lock(g_cache_mutex);
	auto it = g_location_cache_index.find(cache_key);
	if (it != g_location_cache_index.end()) {
		size_t idx = it->second;

		if (idx >= g_location_cache_lru.size()) {
			_RebuildCacheIndex();
			it = g_location_cache_index.find(cache_key);
			if (it == g_location_cache_index.end()) return std::nullopt;
			idx = it->second;
		}

		auto entry = g_location_cache_lru[idx].second;
		entry.last_used = std::chrono::steady_clock::now();

		if (idx > 0) {
			auto item = g_location_cache_lru[idx];
			g_location_cache_lru.erase(g_location_cache_lru.begin() + idx);
			g_location_cache_lru.push_front(item);
			_RebuildCacheIndex();
		}

		return entry;
	}
	return std::nullopt;
}

void UpdateCachedLocation(const std::wstring& cache_key, const CacheEntry& entry) {
	std::unique_lock lock(g_cache_mutex);

	auto it = g_location_cache_index.find(cache_key);
	if (it != g_location_cache_index.end()) {
		size_t idx = it->second;
		g_location_cache_lru[idx].second = entry;

		if (idx > 0) {
			auto item = g_location_cache_lru[idx];
			g_location_cache_lru.erase(g_location_cache_lru.begin() + idx);
			g_location_cache_lru.push_front(item);
			_RebuildCacheIndex();
		}
	}
	else {
		g_location_cache_lru.push_front({ cache_key, entry });

		while (g_location_cache_lru.size() > MAX_CACHED_LOCATIONS) {
			g_location_cache_lru.pop_back();
		}
		_RebuildCacheIndex();
	}
}

struct BitmapCacheEntry {
	std::shared_ptr<PixelBuffer> buffer;
	std::wstring key;
};

std::deque<BitmapCacheEntry> g_bitmap_cache;
std::mutex g_bitmap_cache_mutex;
std::unordered_map<std::wstring, size_t> g_bitmap_cache_index;

void _RebuildBitmapCacheIndex() {
	g_bitmap_cache_index.clear();
	for (size_t i = 0; i < g_bitmap_cache.size(); ++i) {
		g_bitmap_cache_index[g_bitmap_cache[i].key] = i;
	}
}

std::shared_ptr<PixelBuffer> GetCachedBitmap(const std::wstring& key) {
	std::lock_guard<std::mutex> lock(g_bitmap_cache_mutex);

	auto it = g_bitmap_cache_index.find(key);
	if (it != g_bitmap_cache_index.end()) {
		size_t idx = it->second;
		auto entry = g_bitmap_cache[idx];

		if (idx > 0) {
			g_bitmap_cache.erase(g_bitmap_cache.begin() + idx);
			g_bitmap_cache.push_front(entry);
			_RebuildBitmapCacheIndex();
		}
		return entry.buffer;
	}
	return nullptr;
}

void CacheBitmap(const std::wstring& key, std::shared_ptr<PixelBuffer> buffer) {
	std::lock_guard<std::mutex> lock(g_bitmap_cache_mutex);

	for (auto it = g_bitmap_cache.begin(); it != g_bitmap_cache.end(); ++it) {
		if (it->key == key) {
			g_bitmap_cache.erase(it);
			break;
		}
	}

	g_bitmap_cache.push_front({ buffer, key });

	while (g_bitmap_cache.size() > MAX_CACHED_BITMAPS) {
		g_bitmap_cache.pop_back();
	}
}

struct PixelBufferPool {
	std::vector<std::vector<COLORREF>> buffers;
	std::mutex mutex;
	std::unordered_map<size_t, std::vector<std::vector<COLORREF>>> size_buckets;

	std::vector<COLORREF> Acquire(size_t size) {
		std::lock_guard<std::mutex> lock(mutex);

		size_t bucket_size = (size + 1023) / 1024 * 1024;

		auto& bucket = size_buckets[bucket_size];
		if (!bucket.empty()) {
			auto buf = std::move(bucket.back());
			bucket.pop_back();
			buf.resize(size);
			return buf;
		}

		return std::vector<COLORREF>(size);
	}

	void Release(std::vector<COLORREF>&& buffer) {
		if (buffer.empty()) return;
		std::lock_guard<std::mutex> lock(mutex);
		
		// CRITICAL FIX: Add to size_buckets (not just buffers) to match Acquire() logic
		size_t buffer_capacity = buffer.capacity();
		size_t bucket_size = (buffer_capacity + 1023) / 1024 * 1024;
		
		// Count total buffers across all buckets
		size_t total_buffers = buffers.size();
		for (const auto& [size, vec] : size_buckets) {
			total_buffers += vec.size();
		}
		
		int max_size = g_pixel_pool_size.load(std::memory_order_relaxed);
		if (total_buffers < static_cast<size_t>(max_size)) {
			size_buckets[bucket_size].push_back(std::move(buffer));
		}
	}
};

PixelBufferPool g_pixel_pool;

// PixelBuffer special member implementations
PixelBuffer::~PixelBuffer() {
	if (owns_memory && !pixels.empty()) {
		g_pixel_pool.Release(std::move(pixels));
	}
}

PixelBuffer::PixelBuffer(PixelBuffer&& other) noexcept
	: pixels(std::move(other.pixels))
	, width(other.width)
	, height(other.height)
	, has_alpha(other.has_alpha)
	, owns_memory(other.owns_memory) {
	other.owns_memory = false;
}

PixelBuffer& PixelBuffer::operator=(PixelBuffer&& other) noexcept {
	if (this != &other) {
		if (owns_memory && !pixels.empty()) {
			g_pixel_pool.Release(std::move(pixels));
		}
		pixels = std::move(other.pixels);
		width = other.width;
		height = other.height;
		has_alpha = other.has_alpha;
		owns_memory = other.owns_memory;
		other.owns_memory = false;
	}
	return *this;
}

bool DetectAlphaChannel(const PixelBuffer& buffer) {
	size_t sample_size = std::min<size_t>(buffer.pixels.size(), 1000);
	size_t sample_step = std::max<size_t>(1, buffer.pixels.size() / sample_size);

	for (size_t i = 0; i < buffer.pixels.size(); i += sample_step) {
		uint8_t alpha = (buffer.pixels[i] >> 24) & 0xFF;
		if (alpha < 255) {
			return true;
		}
	}
	return false;
}

std::optional<PixelBuffer> LoadImageFromFile_GDI(const std::wstring& file_path) {
	InitializeGdiplus();

	std::wstring cache_key = L"DECODE_" + GetNormalizedPathKey(file_path);
	auto cached = GetCachedBitmap(cache_key);
	if (cached) {
		PixelBuffer result;
		result.width = cached->width;
		result.height = cached->height;
		result.has_alpha = cached->has_alpha;
		result.pixels = cached->pixels;
		result.owns_memory = false;
		return result;
	}

	auto bitmap = std::make_unique<Bitmap>(file_path.c_str());
	if (!bitmap) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return std::nullopt;
	}

	Status status = bitmap->GetLastStatus();
	if (status != Ok) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return std::nullopt;
	}

	int width = bitmap->GetWidth();
	int height = bitmap->GetHeight();
	if (width <= 0 || height <= 0 || width > 32000 || height > 32000) {
		return std::nullopt;
	}

	PixelFormat format = bitmap->GetPixelFormat();
	bool hasAlpha = (format & PixelFormatAlpha) || (format == PixelFormat32bppARGB);

	BitmapData bitmapData;
	Rect rect(0, 0, width, height);

	if (bitmap->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Ok) {
		return std::nullopt;
	}

	PixelBuffer buffer;
	buffer.width = width;
	buffer.height = height;
	buffer.pixels = g_pixel_pool.Acquire(width * height);
	if (buffer.pixels.capacity() < static_cast<size_t>(width * height)) {
		bitmap->UnlockBits(&bitmapData);
		return std::nullopt;
	}
	buffer.pixels.resize(width * height);

	BYTE* pixels = (BYTE*)bitmapData.Scan0;
	int stride = bitmapData.Stride;

	for (int y = 0; y < height; y++) {
		DWORD* row = (DWORD*)(pixels + y * stride);
		for (int x = 0; x < width; x++) {
			DWORD argb = row[x];
			BYTE a = (argb >> 24) & 0xFF;
			BYTE r = (argb >> 16) & 0xFF;
			BYTE g = (argb >> 8) & 0xFF;
			BYTE b = argb & 0xFF;

			if (a != 0 && a != 255) {
				float invA = 255.0f / static_cast<float>(a);
				int ur = static_cast<int>(std::round(r * invA));
				int ug = static_cast<int>(std::round(g * invA));
				int ub = static_cast<int>(std::round(b * invA));
				r = static_cast<BYTE>(std::clamp(ur, 0, 255));
				g = static_cast<BYTE>(std::clamp(ug, 0, 255));
				b = static_cast<BYTE>(std::clamp(ub, 0, 255));
			}
			buffer.pixels[y * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}

	bitmap->UnlockBits(&bitmapData);

	buffer.has_alpha = hasAlpha && DetectAlphaChannel(buffer);

	auto shared_buffer = std::make_shared<PixelBuffer>();
	shared_buffer->width = buffer.width;
	shared_buffer->height = buffer.height;
	shared_buffer->has_alpha = buffer.has_alpha;
	shared_buffer->pixels = buffer.pixels;
	shared_buffer->owns_memory = false;

	buffer.owns_memory = false;

	CacheBitmap(cache_key, shared_buffer);

	return buffer;
}

std::optional<PixelBuffer> ScaleBitmap_GDI(const PixelBuffer& source, int newW, int newH) {
	if (!source.IsValid()) return std::nullopt;
	if (newW <= 0 || newH <= 0 || newW > 32000 || newH > 32000) return std::nullopt;

	size_t source_hash = 0;
	size_t sample_step = std::max<size_t>(1, source.pixels.size() / 100);
	for (size_t i = 0; i < source.pixels.size(); i += sample_step) {
		source_hash ^= std::hash<COLORREF>{}(source.pixels[i]) + 0x9e3779b9 + (source_hash << 6) + (source_hash >> 2);
	}

	std::wstringstream cache_key_ss;
	cache_key_ss << L"SCALED_" << std::hex << source_hash << L"_"
		<< std::dec << source.width << L"x" << source.height
		<< L"_to_" << newW << L"x" << newH;
	std::wstring cache_key = cache_key_ss.str();

	auto cached = GetCachedBitmap(cache_key);
	if (cached) {
		PixelBuffer result;
		result.width = cached->width;
		result.height = cached->height;
		result.has_alpha = cached->has_alpha;
		result.pixels = cached->pixels;
		result.owns_memory = false;
		return result;
	}

	InitializeGdiplus();

	Bitmap srcBitmap(source.width, source.height, PixelFormat32bppARGB);
	BitmapData srcData;
	Rect srcRect(0, 0, source.width, source.height);

	if (srcBitmap.LockBits(&srcRect, ImageLockModeWrite, PixelFormat32bppARGB, &srcData) != Ok) {
		return std::nullopt;
	}

	BYTE* srcPixels = (BYTE*)srcData.Scan0;
	int srcStride = srcData.Stride;

	for (int y = 0; y < source.height; y++) {
		DWORD* row = (DWORD*)(srcPixels + y * srcStride);
		for (int x = 0; x < source.width; x++) {
			COLORREF pixel = source.pixels[y * source.width + x];
			BYTE a = (pixel >> 24) & 0xFF;
			BYTE b = (pixel >> 16) & 0xFF;
			BYTE g = (pixel >> 8) & 0xFF;
			BYTE r = pixel & 0xFF;

			if (a != 0 && a != 255) {
				float invA = 255.0f / static_cast<float>(a);
				int ur = static_cast<int>(std::round(r * invA));
				int ug = static_cast<int>(std::round(g * invA));
				int ub = static_cast<int>(std::round(b * invA));
				r = static_cast<BYTE>(std::clamp(ur, 0, 255));
				g = static_cast<BYTE>(std::clamp(ug, 0, 255));
				b = static_cast<BYTE>(std::clamp(ub, 0, 255));
			}
			row[x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	srcBitmap.UnlockBits(&srcData);

	Bitmap dstBitmap(newW, newH, PixelFormat32bppARGB);
	Graphics graphics(&dstBitmap);
	graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
	graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
	graphics.SetSmoothingMode(SmoothingModeHighQuality);

	if (graphics.DrawImage(&srcBitmap, 0, 0, newW, newH) != Ok) {
		return std::nullopt;
	}

	BitmapData dstData;
	Rect dstRect(0, 0, newW, newH);

	if (dstBitmap.LockBits(&dstRect, ImageLockModeRead, PixelFormat32bppARGB, &dstData) != Ok) {
		return std::nullopt;
	}

	PixelBuffer result;
	result.width = newW;
	result.height = newH;
	result.has_alpha = source.has_alpha;
	result.pixels = g_pixel_pool.Acquire(newW * newH);
	if (result.pixels.capacity() < static_cast<size_t>(newW * newH)) {
		dstBitmap.UnlockBits(&dstData);
		return std::nullopt;
	}
	result.pixels.resize(newW * newH);

	BYTE* dstPixels = (BYTE*)dstData.Scan0;
	int dstStride = dstData.Stride;

	for (int y = 0; y < newH; y++) {
		DWORD* row = (DWORD*)(dstPixels + y * dstStride);
		for (int x = 0; x < newW; x++) {
			DWORD argb = row[x];
			BYTE a = (argb >> 24) & 0xFF;
			BYTE r = (argb >> 16) & 0xFF;
			BYTE g = (argb >> 8) & 0xFF;
			BYTE b = argb & 0xFF;

			if (a != 0 && a != 255) {
				float invA = 255.0f / static_cast<float>(a);
				int ur = static_cast<int>(std::round(r * invA));
				int ug = static_cast<int>(std::round(g * invA));
				int ub = static_cast<int>(std::round(b * invA));
				r = static_cast<BYTE>(std::clamp(ur, 0, 255));
				g = static_cast<BYTE>(std::clamp(ug, 0, 255));
				b = static_cast<BYTE>(std::clamp(ub, 0, 255));
			}
			result.pixels[y * newW + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}

	dstBitmap.UnlockBits(&dstData);

	auto shared_result = std::make_shared<PixelBuffer>();
	shared_result->width = result.width;
	shared_result->height = result.height;
	shared_result->has_alpha = result.has_alpha;
	shared_result->pixels = result.pixels;
	shared_result->owns_memory = false;

	result.owns_memory = false;

	CacheBitmap(cache_key, shared_result);

	return result;
}

std::optional<PixelBuffer> GetBitmapPixels_GDI(HBITMAP hBitmap) {
	if (!hBitmap) return std::nullopt;

	InitializeGdiplus();

	BITMAP bm;
	if (!GetObject(hBitmap, sizeof(BITMAP), &bm)) return std::nullopt;

	int width = bm.bmWidth;
	int height = bm.bmHeight;
	if (width <= 0 || height <= 0 || width > 32000 || height > 32000) return std::nullopt;

	auto bitmap = std::unique_ptr<Bitmap>(Bitmap::FromHBITMAP(hBitmap, NULL));
	if (!bitmap || bitmap->GetLastStatus() != Ok) {
		return std::nullopt;
	}

	BitmapData bitmapData;
	Rect rect(0, 0, width, height);

	if (bitmap->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Ok) {
		return std::nullopt;
	}

	PixelBuffer buffer;
	buffer.width = width;
	buffer.height = height;

	size_t pixel_count = static_cast<size_t>(width) * height;
	if (pixel_count > 100000000) {
		bitmap->UnlockBits(&bitmapData);
		return std::nullopt;
	}

	buffer.pixels = g_pixel_pool.Acquire(pixel_count);
	buffer.pixels.resize(pixel_count);

	BYTE* pixels = (BYTE*)bitmapData.Scan0;
	int stride = bitmapData.Stride;

	for (int y = 0; y < height; y++) {
		DWORD* row = (DWORD*)(pixels + y * stride);
		for (int x = 0; x < width; x++) {
			DWORD argb = row[x];
			BYTE a = (argb >> 24) & 0xFF;
			BYTE r = (argb >> 16) & 0xFF;
			BYTE g = (argb >> 8) & 0xFF;
			BYTE b = argb & 0xFF;

			if (a != 0 && a != 255) {
				float invA = 255.0f / static_cast<float>(a);
				int ur = static_cast<int>(std::round(r * invA));
				int ug = static_cast<int>(std::round(g * invA));
				int ub = static_cast<int>(std::round(b * invA));
				r = static_cast<BYTE>(std::clamp(ur, 0, 255));
				g = static_cast<BYTE>(std::clamp(ug, 0, 255));
				b = static_cast<BYTE>(std::clamp(ub, 0, 255));
			}
			buffer.pixels[y * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}

	bitmap->UnlockBits(&bitmapData);

	buffer.has_alpha = DetectAlphaChannel(buffer);

	return buffer;
}

struct MonitorInfo {
	RECT bounds;
	int index;
};

std::vector<MonitorInfo> g_monitors;
std::mutex g_monitors_mutex;

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
	MonitorInfo info;
	info.bounds = *lprcMonitor;
	info.index = static_cast<int>(monitors->size());
	monitors->push_back(info);
	return TRUE;
}

void EnumerateMonitors() {
	std::lock_guard<std::mutex> lock(g_monitors_mutex);
	g_monitors.clear();
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&g_monitors));
}

bool GetMonitorBounds(int screen_index, RECT& bounds) {
	std::lock_guard<std::mutex> lock(g_monitors_mutex);

	// SAFETY FIX: Always re-enumerate to avoid stale data (e.g., when monitors are plugged/unplugged)
	g_monitors.clear();
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&g_monitors));

	// BOUNDS CHECK: Validate screen_index range
	if (screen_index <= 0 || screen_index > static_cast<int>(g_monitors.size()) || g_monitors.empty()) {
		return false;
	}

	bounds = g_monitors[screen_index - 1].bounds;
	return true;
}

// Helper: Get screen bounds with automatic fallback
static void GetScreenBounds(int iScreen, int& left, int& top, int& width, int& height) {
	if (iScreen > 0) {
		RECT monitorBounds;
		if (GetMonitorBounds(iScreen, monitorBounds)) {
			left = monitorBounds.left;
			top = monitorBounds.top;
			width = monitorBounds.right - monitorBounds.left;
			height = monitorBounds.bottom - monitorBounds.top;
			return;
		}
		else {
			left = 0; top = 0;
			width = GetSystemMetrics(SM_CXSCREEN);
			height = GetSystemMetrics(SM_CYSCREEN);
		}
	}
	else if (iScreen == 0) {
		left = top = 0;
		width = height = 0;
	}
	else { // iScreen < 0
		left = GetSystemMetrics(SM_XVIRTUALSCREEN);
		top = GetSystemMetrics(SM_YVIRTUALSCREEN);
		width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	}
}

// Internal unified screen capture function
static HBITMAP CaptureScreenInternal(int iLeft, int iTop, int iRight, int iBottom, int iScreen) {
	int screenLeft, screenTop, screenWidth, screenHeight;
	GetScreenBounds(iScreen, screenLeft, screenTop, screenWidth, screenHeight);

	// FIX: When user passes (0,0,0,0) for specific monitor, they want full monitor capture
	// Convert to monitor-local coordinates
	if (iScreen > 0 && iLeft == 0 && iTop == 0 && iRight == 0 && iBottom == 0) {
		iLeft = screenLeft;
		iTop = screenTop;
		iRight = screenLeft + screenWidth;
		iBottom = screenTop + screenHeight;
	}
	else {
		if (iRight == -1 || iRight == 0) iRight = screenLeft + screenWidth;
		if (iBottom == -1 || iBottom == 0) iBottom = screenTop + screenHeight;

		iLeft = std::clamp(iLeft, screenLeft, screenLeft + screenWidth - 1);
		iTop = std::clamp(iTop, screenTop, screenTop + screenHeight - 1);
		iRight = std::clamp(iRight, iLeft + 1, screenLeft + screenWidth);
		iBottom = std::clamp(iBottom, iTop + 1, screenTop + screenHeight);
	}

	int width = iRight - iLeft;
	int height = iBottom - iTop;

	if (width <= 0 || height <= 0 || width > 32000 || height > 32000) return nullptr;

	HDC hdcScreen = GetDC(nullptr);
	if (!hdcScreen) return nullptr;

	HDC hdcMem = CreateCompatibleDC(hdcScreen);
	if (!hdcMem) {
		ReleaseDC(nullptr, hdcScreen);
		return nullptr;
	}

	HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
	if (!hBitmap) {
		DeleteDC(hdcMem);
		ReleaseDC(nullptr, hdcScreen);
		return nullptr;
	}

	HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
	BOOL success = BitBlt(hdcMem, 0, 0, width, height, hdcScreen, iLeft, iTop, SRCCOPY);

	SelectObject(hdcMem, hOldBitmap);
	DeleteDC(hdcMem);
	ReleaseDC(nullptr, hdcScreen);

	if (!success) {
		DeleteObject(hBitmap);
		return nullptr;
	}

	return hBitmap;
}

std::optional<PixelBuffer> CaptureScreen_GDI(int iLeft, int iTop, int iRight, int iBottom, int iScreen = 0) {
	HBITMAP hBitmap = CaptureScreenInternal(iLeft, iTop, iRight, iBottom, iScreen);
	if (!hBitmap) return std::nullopt;

	auto result = GetBitmapPixels_GDI(hBitmap);
	DeleteObject(hBitmap);

	return result;
}

namespace PixelComparison {
	// Helper: Check if search region is valid
	inline bool IsValidSearchRegion(int start_x, int start_y, int source_width, int source_height,
		int screen_width, int screen_height) noexcept {
		return start_x >= 0 && start_y >= 0 &&
			start_x + source_width <= screen_width &&
			start_y + source_height <= screen_height;
	}

	inline bool CheckApproxMatch_Scalar(
		const PixelBuffer& screen, const PixelBuffer& source,
		int start_x, int start_y, bool transparent_enabled, int tolerance) noexcept {

		if (!IsValidSearchRegion(start_x, start_y, source.width, source.height, screen.width, screen.height)) {
			return false;
		}

		int alpha_threshold = ComputeAlphaThreshold(transparent_enabled, tolerance);

		for (int y = 0; y < source.height; ++y) {
			const COLORREF* source_row = &source.pixels[y * source.width];
			const COLORREF* screen_row = &screen.pixels[(start_y + y) * screen.width + start_x];

			for (int x = 0; x < source.width; ++x) {
				COLORREF source_pixel = source_row[x];

				if (transparent_enabled) {
					uint8_t alpha = (source_pixel >> 24) & 0xFF;
					if (alpha < alpha_threshold) continue;
				}

				COLORREF screen_pixel = screen_row[x];
				if (std::abs((int)GetRValue(source_pixel) - (int)GetRValue(screen_pixel)) > tolerance ||
					std::abs((int)GetGValue(source_pixel) - (int)GetGValue(screen_pixel)) > tolerance ||
					std::abs((int)GetBValue(source_pixel) - (int)GetBValue(screen_pixel)) > tolerance) {
					return false;
				}
			}
		}
		return true;
	}

#ifdef _WIN64
	inline bool CheckApproxMatch_AVX2(
		const PixelBuffer& screen, const PixelBuffer& source,
		int start_x, int start_y, bool transparent_enabled, int tolerance) noexcept {

		if (!IsValidSearchRegion(start_x, start_y, source.width, source.height, screen.width, screen.height)) {
			return false;
		}

		int alpha_threshold = ComputeAlphaThreshold(transparent_enabled, tolerance);

		const __m256i v_alpha_threshold = _mm256_set1_epi32(alpha_threshold);
		const __m256i v_rgb_mask = _mm256_set1_epi32(0x00FFFFFF);
		const __m256i v_tolerance8 = _mm256_set1_epi8(static_cast<char>(tolerance));

		for (int y = 0; y < source.height; ++y) {
			const COLORREF* source_row = &source.pixels[y * source.width];
			const COLORREF* screen_row = &screen.pixels[(start_y + y) * screen.width + start_x];

			int x = 0;
			for (; x + 7 < source.width; x += 8) {
				__m256i v_source = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source_row + x));

				__m256i v_transparent_mask = _mm256_setzero_si256();
				if (transparent_enabled) {
					__m256i v_alpha = _mm256_srli_epi32(v_source, 24);
					__m256i v_is_transparent = _mm256_cmpgt_epi32(v_alpha_threshold, v_alpha);
					v_transparent_mask = v_is_transparent;
				}

				if (transparent_enabled && _mm256_testc_si256(v_transparent_mask, _mm256_set1_epi32(-1))) {
					continue;
				}

				__m256i v_screen = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(screen_row + x));

				__m256i v_source_rgb = _mm256_and_si256(v_source, v_rgb_mask);
				__m256i v_screen_rgb = _mm256_and_si256(v_screen, v_rgb_mask);

				__m256i v_diff1 = _mm256_subs_epu8(v_source_rgb, v_screen_rgb);
				__m256i v_diff2 = _mm256_subs_epu8(v_screen_rgb, v_source_rgb);
				__m256i v_abs_diff = _mm256_or_si256(v_diff1, v_diff2);

				__m256i v_check = _mm256_subs_epu8(v_abs_diff, v_tolerance8);

				__m256i v_mismatch = _mm256_andnot_si256(v_transparent_mask, v_check);

				if (!_mm256_testz_si256(v_mismatch, v_mismatch)) {
					return false;
				}
			}

			for (; x < source.width; ++x) {
				COLORREF source_pixel = source_row[x];

				if (transparent_enabled) {
					uint8_t alpha = (source_pixel >> 24) & 0xFF;
					if (alpha < alpha_threshold) continue;
				}

				COLORREF screen_pixel = screen_row[x];
				if (std::abs((int)GetRValue(source_pixel) - (int)GetRValue(screen_pixel)) > tolerance ||
					std::abs((int)GetGValue(source_pixel) - (int)GetGValue(screen_pixel)) > tolerance ||
					std::abs((int)GetBValue(source_pixel) - (int)GetBValue(screen_pixel)) > tolerance) {
					return false;
				}
			}
		}
		return true;
	}

	inline bool CheckApproxMatch_AVX512(
		const PixelBuffer& screen, const PixelBuffer& source,
		int start_x, int start_y, bool transparent_enabled, int tolerance) noexcept {

		if (!IsValidSearchRegion(start_x, start_y, source.width, source.height, screen.width, screen.height)) {
			return false;
		}

		int alpha_threshold = ComputeAlphaThreshold(transparent_enabled, tolerance);

		const __m512i v_alpha_threshold = _mm512_set1_epi32(alpha_threshold << 24);
		const __m512i v_rgb_mask = _mm512_set1_epi32(0x00FFFFFF);
		const __m512i v_tolerance8 = _mm512_set1_epi8(static_cast<char>(tolerance));

		for (int y = 0; y < source.height; ++y) {
			const COLORREF* source_row = &source.pixels[y * source.width];
			const COLORREF* screen_row = &screen.pixels[(start_y + y) * screen.width + start_x];

			int x = 0;
			for (; x + 15 < source.width; x += 16) {
				__m512i v_source = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(source_row + x));

				__mmask16 transparent_mask = 0;
				if (transparent_enabled) {
					__m512i v_alpha = _mm512_srli_epi32(v_source, 24);
					transparent_mask = _mm512_cmp_epi32_mask(v_alpha, _mm512_set1_epi32(alpha_threshold), _MM_CMPINT_LT);
				}

				if (transparent_enabled && transparent_mask == 0xFFFF) {
					continue;
				}

				__m512i v_screen = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(screen_row + x));

				__m512i v_source_rgb = _mm512_and_si512(v_source, v_rgb_mask);
				__m512i v_screen_rgb = _mm512_and_si512(v_screen, v_rgb_mask);

				__m512i v_diff1 = _mm512_subs_epu8(v_source_rgb, v_screen_rgb);
				__m512i v_diff2 = _mm512_subs_epu8(v_screen_rgb, v_source_rgb);
				__m512i v_abs_diff = _mm512_or_si512(v_diff1, v_diff2);

				__mmask64 v_exceed = _mm512_cmp_epu8_mask(v_abs_diff, v_tolerance8, _MM_CMPINT_GT);

				__mmask64 transparent_byte_mask = 0;
				if (transparent_enabled && transparent_mask != 0) {
					uint64_t expanded = 0;
					for (int p = 0; p < 16; ++p) {
						if (transparent_mask & (1 << p)) {
							expanded |= (0xFULL << (p * 4));
						}
					}
					transparent_byte_mask = expanded;
				}

				__mmask64 v_mismatch = v_exceed & ~transparent_byte_mask;

				if (v_mismatch != 0) {
					return false;
				}
			}

			if (g_is_avx2_supported.load(std::memory_order_relaxed)) {
				for (; x + 7 < source.width; x += 8) {
					__m256i v_source = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source_row + x));
					__m256i v_transparent_mask = _mm256_setzero_si256();

					if (transparent_enabled) {
						__m256i v_alpha = _mm256_and_si256(v_source, _mm256_set1_epi32(0xFF000000));
						__m256i v_is_transparent = _mm256_cmpgt_epi32(_mm256_set1_epi32(alpha_threshold << 24), v_alpha);
						v_transparent_mask = v_is_transparent;
					}

					if (transparent_enabled && _mm256_testc_si256(v_transparent_mask, _mm256_set1_epi32(-1))) {
						continue;
					}

					__m256i v_screen = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(screen_row + x));
					__m256i v_source_rgb = _mm256_and_si256(v_source, _mm256_set1_epi32(0x00FFFFFF));
					__m256i v_screen_rgb = _mm256_and_si256(v_screen, _mm256_set1_epi32(0x00FFFFFF));

					__m256i v_diff1 = _mm256_subs_epu8(v_source_rgb, v_screen_rgb);
					__m256i v_diff2 = _mm256_subs_epu8(v_screen_rgb, v_source_rgb);
					__m256i v_abs_diff = _mm256_or_si256(v_diff1, v_diff2);

					__m256i v_check = _mm256_subs_epu8(v_abs_diff, _mm256_set1_epi8(static_cast<char>(tolerance)));
					__m256i v_mismatch = _mm256_andnot_si256(v_transparent_mask, v_check);

					if (!_mm256_testz_si256(v_mismatch, v_mismatch)) {
						return false;
					}
				}
			}

			for (; x < source.width; ++x) {
				COLORREF source_pixel = source_row[x];

				if (transparent_enabled) {
					uint8_t alpha = (source_pixel >> 24) & 0xFF;
					if (alpha < alpha_threshold) continue;
				}

				COLORREF screen_pixel = screen_row[x];
				if (std::abs((int)GetRValue(source_pixel) - (int)GetRValue(screen_pixel)) > tolerance ||
					std::abs((int)GetGValue(source_pixel) - (int)GetGValue(screen_pixel)) > tolerance ||
					std::abs((int)GetBValue(source_pixel) - (int)GetBValue(screen_pixel)) > tolerance) {
					return false;
				}
			}
		}
		return true;
	}
#else
	inline bool CheckApproxMatch_SSE2(
		const PixelBuffer& screen, const PixelBuffer& source,
		int start_x, int start_y, bool transparent_enabled, int tolerance) noexcept {

		if (!IsValidSearchRegion(start_x, start_y, source.width, source.height, screen.width, screen.height)) {
			return false;
		}

		int alpha_threshold = ComputeAlphaThreshold(transparent_enabled, tolerance);

		const __m128i v_alpha_threshold = _mm_set1_epi32(alpha_threshold << 24);
		const __m128i v_rgb_mask = _mm_set1_epi32(0x00FFFFFF);
		const __m128i v_tolerance = _mm_set1_epi16(tolerance);
		const __m128i v_zero = _mm_setzero_si128();

		for (int y = 0; y < source.height; ++y) {
			const COLORREF* source_row = &source.pixels[y * source.width];
			const COLORREF* screen_row = &screen.pixels[(start_y + y) * screen.width + start_x];

			int x = 0;
			for (; x + 3 < source.width; x += 4) {
				__m128i v_source = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source_row + x));

				__m128i v_transparent_mask = _mm_setzero_si128();
				if (transparent_enabled) {
					__m128i v_alpha = _mm_srli_epi32(v_source, 24);
					__m128i v_alpha_compare = _mm_cmplt_epi32(v_alpha, _mm_set1_epi32(alpha_threshold));
					v_transparent_mask = v_alpha_compare;
				}

				int mask = _mm_movemask_epi8(v_transparent_mask);
				if (transparent_enabled && mask == 0xFFFF) {
					continue;
				}

				__m128i v_screen = _mm_loadu_si128(reinterpret_cast<const __m128i*>(screen_row + x));

				__m128i v_source_rgb = _mm_and_si128(v_source, v_rgb_mask);
				__m128i v_screen_rgb = _mm_and_si128(v_screen, v_rgb_mask);

				__m128i v_source_lo = _mm_unpacklo_epi8(v_source_rgb, v_zero);
				__m128i v_source_hi = _mm_unpackhi_epi8(v_source_rgb, v_zero);
				__m128i v_screen_lo = _mm_unpacklo_epi8(v_screen_rgb, v_zero);
				__m128i v_screen_hi = _mm_unpackhi_epi8(v_screen_rgb, v_zero);

				__m128i v_diff_lo = _mm_sub_epi16(v_source_lo, v_screen_lo);
				__m128i v_diff_hi = _mm_sub_epi16(v_source_hi, v_screen_hi);

				__m128i v_abs_diff_lo = _mm_max_epi16(v_diff_lo, _mm_sub_epi16(v_zero, v_diff_lo));
				__m128i v_abs_diff_hi = _mm_max_epi16(v_diff_hi, _mm_sub_epi16(v_zero, v_diff_hi));

				__m128i v_check_lo = _mm_cmpgt_epi16(v_abs_diff_lo, v_tolerance);
				__m128i v_check_hi = _mm_cmpgt_epi16(v_abs_diff_hi, v_tolerance);

				__m128i v_check = _mm_packs_epi16(v_check_lo, v_check_hi);
				__m128i v_mismatch = _mm_andnot_si128(v_transparent_mask, v_check);

				if (_mm_movemask_epi8(v_mismatch) != 0) {
					return false;
				}
			}

			for (; x < source.width; ++x) {
				COLORREF source_pixel = source_row[x];

				if (transparent_enabled) {
					uint8_t alpha = (source_pixel >> 24) & 0xFF;
					if (alpha < alpha_threshold) continue;
				}

				COLORREF screen_pixel = screen_row[x];
				if (std::abs((int)GetRValue(source_pixel) - (int)GetRValue(screen_pixel)) > tolerance ||
					std::abs((int)GetGValue(source_pixel) - (int)GetGValue(screen_pixel)) > tolerance ||
					std::abs((int)GetBValue(source_pixel) - (int)GetBValue(screen_pixel)) > tolerance) {
					return false;
				}
			}
		}
		return true;
	}
#endif
}

// ============================================================================
// HELPER FUNCTION: CompareMatchResults
// ============================================================================
// Description:
//   Comparison function for sorting match results in top-to-bottom, left-to-right order.
//   Used by std::sort to organize multiple matches into a consistent order.
// ============================================================================
bool CompareMatchResults(const MatchResult& a, const MatchResult& b) {
	if (a.y != b.y) return a.y < b.y;  // Sort by Y first (top to bottom)
	return a.x < b.x;                  // Then by X (left to right)
}

// ============================================================================
// CORE ALGORITHM: SearchForBitmap
// ============================================================================
// Description:
//   Main image searching algorithm. Performs pixel-by-pixel comparison with
//   SIMD optimization (AVX512/AVX2/SSE2) and optional multi-threading.
//
// Algorithm:
//   1. Detect CPU capabilities and select fastest SIMD backend
//   2. For large images (>500px height) and find_all mode, use multi-threading
//   3. Scan source image row-by-row, column-by-column
//   4. For each position, call SIMD-optimized pixel comparison
//   5. Collect all matches or stop at first match based on find_all flag
//
// Performance Optimizations:
//   - SIMD instructions process 8-16 pixels simultaneously
//   - Multi-threading divides work across CPU cores
//   - Early-exit on first match when find_all=false
//   - Cache-friendly row-wise scanning
//
// Parameters:
//   Source           - Screen/source image to search within
//   Target           - Template image to find
//   search_left/top  - Offset to add to result coordinates
//   tolerance        - Color matching tolerance (0-255)
//   transparent_enabled - Skip pixels with low alpha channel
//   find_all         - Find all matches (true) or first only (false)
//   scale_factor     - Scale factor of current search (for reporting)
//   source_file      - Image filename (for debugging)
//   backend_used     - Output: SIMD backend actually used
//
// Returns:
//   Vector of MatchResult containing all found positions
// ============================================================================
std::vector<MatchResult> SearchForBitmap(
	const PixelBuffer& Source, const PixelBuffer& Target,
	int search_left, int search_top, int tolerance, bool transparent_enabled,
	bool find_all, float scale_factor, const std::wstring& source_file,
	std::wstring& backend_used) {

	std::vector<MatchResult> matches;
	if (Target.width > Source.width || Target.height > Source.height) return matches;

	backend_used = L"Scalar";

	auto CheckMatch = [&](int x, int y) -> bool {
#ifdef _WIN64
		if (g_is_avx512_supported.load(std::memory_order_relaxed)) {
			return PixelComparison::CheckApproxMatch_AVX512(Source, Target, x, y, transparent_enabled, tolerance);
		}
		else if (g_is_avx2_supported.load(std::memory_order_relaxed)) {
			return PixelComparison::CheckApproxMatch_AVX2(Source, Target, x, y, transparent_enabled, tolerance);
		}
		else {
			return PixelComparison::CheckApproxMatch_Scalar(Source, Target, x, y, transparent_enabled, tolerance);
		}
#else
		if (g_is_sse2_supported.load(std::memory_order_relaxed)) {
			return PixelComparison::CheckApproxMatch_SSE2(Source, Target, x, y, transparent_enabled, tolerance);
		}
		else {
			return PixelComparison::CheckApproxMatch_Scalar(Source, Target, x, y, transparent_enabled, tolerance);
		}
#endif
		};

#ifdef _WIN64
	if (g_is_avx512_supported.load(std::memory_order_relaxed)) {
		backend_used = L"AVX512";
	}
	else if (g_is_avx2_supported.load(std::memory_order_relaxed)) {
		backend_used = L"AVX2";
	}
	else {
		backend_used = L"Scalar";
	}
#else
	if (g_is_sse2_supported.load(std::memory_order_relaxed)) {
		backend_used = L"SSE2";
	}
	else {
		backend_used = L"Scalar";
	}
#endif

	// ========================================================================
	// MULTI-THREADING OPTIMIZATION
	// ========================================================================
	// For large images (>500px height) and find_all mode, split work across
	// CPU cores for maximum performance. Each thread searches a vertical slice.
	// ========================================================================
	if (find_all && Source.height > 500) {
		unsigned int num_threads = std::max(1u, std::thread::hardware_concurrency());
		int chunk_height = (Source.height - Target.height + 1) / num_threads;

		// Don't use threading if chunks would be too small (overhead > benefit)
		if (chunk_height < 50) {
			num_threads = 1;
		}

		if (num_threads > 1) {
			// Launch async tasks, each searching a vertical slice
			std::vector<std::future<std::vector<MatchResult>>> futures;

			for (unsigned int t = 0; t < num_threads; ++t) {
				int start_y = t * chunk_height;
				int end_y = (t == num_threads - 1) ? (Source.height - Target.height + 1) : ((t + 1) * chunk_height);

				futures.push_back(std::async(std::launch::async, [&, start_y, end_y]() {
					std::vector<MatchResult> local_matches;

					for (int y = start_y; y < end_y; ++y) {
						for (int x = 0; x <= Source.width - Target.width; ++x) {
							if (CheckMatch(x, y)) {
								local_matches.push_back(MatchResult(x + search_left, y + search_top,
									Target.width, Target.height, scale_factor, source_file));
							}
						}
					}

					return local_matches;
					}));
			}

			for (auto& fut : futures) {
				try {
					fut.wait();
					auto local_results = fut.get();
					matches.insert(matches.end(), local_results.begin(), local_results.end());
				}
				catch (const std::exception&) {
				}
			}

			std::sort(matches.begin(), matches.end(), CompareMatchResults);
			return matches;
		}
	}

	for (int y = 0; y <= Source.height - Target.height; ++y) {
		for (int x = 0; x <= Source.width - Target.width; ++x) {
			bool found = CheckMatch(x, y);
			if (found) {
				matches.push_back(MatchResult(x + search_left, y + search_top, Target.width, Target.height, scale_factor, source_file));
				if (!find_all) {
					return matches;
				}
			}
		}
	}

	return matches;
}

enum class SearchMode {
	ScreenSearch,
	SearchImageInImage,
	HBitmapSearch
};

// =================================================================================================
// SearchParams: Unified parameter structure for all image search operations
// =================================================================================================
// Cache System:
// - use_cache = 0: Disables caching completely (default for compatibility)
// - use_cache = 1: Enables in-memory + persistent disk cache for faster repeated searches
//   * Cache is validated on each lookup to ensure accuracy
//   * Invalid cache entries are automatically removed after 3 misses
//   * Cache persists across DLL reloads for better performance
// =================================================================================================
struct SearchParams {
	SearchMode mode = SearchMode::ScreenSearch;
	const wchar_t* image_files = nullptr;
	int left = 0, top = 0, right = 0, bottom = 0;
	int screen = 0;
	const wchar_t* source_image = nullptr;
	const wchar_t* target_images = nullptr;
	HBITMAP Source_hbitmap = nullptr;
	HBITMAP Target_hbitmap = nullptr;
	int tolerance = 10;
	int max_results = 1;
	int center_pos = 1;
	float min_scale = 1.0f;
	float max_scale = 1.0f;
	float scale_step = 0.1f;
	int return_debug = 0;
	int use_cache = 0;  // 0 = disabled, 1 = enabled
};

std::wstring UnifiedImageSearch(const SearchParams& params) {
	auto start_time = std::chrono::high_resolution_clock::now();

	std::call_once(g_feature_detection_flag, DetectFeatures);
	InitializeGdiplus();

	std::wstringstream result_stream;

	int tolerance = std::clamp(params.tolerance, 0, 255);
	float min_scale = std::clamp(params.min_scale, 0.1f, 5.0f);
	float max_scale = std::clamp(params.max_scale, min_scale, 5.0f);
	float scale_step = std::clamp(params.scale_step, 0.01f, 1.0f);
	scale_step = std::round(scale_step * 10.0f) / 10.0f;

	std::optional<PixelBuffer> Source_opt;
	std::wstring Source_source;
	int search_offset_x = 0, search_offset_y = 0;

	int capture_left = 0, capture_top = 0, capture_right = 0, capture_bottom = 0;
	int capture_width = 0, capture_height = 0;

	if (params.mode == SearchMode::ScreenSearch) {
		int screenLeft, screenTop, screenWidth, screenHeight;
		GetScreenBounds(params.screen, screenLeft, screenTop, screenWidth, screenHeight);

		int screenRight = screenLeft + screenWidth;
		int screenBottom = screenTop + screenHeight;

		int left, top, right, bottom;

		if (params.screen == 0) {
			// iScreen == 0: Use absolute coordinates (no bounds clamping applied)
			left = params.left;
			top = params.top;
			right = (params.right == 0 || params.right == -1) ? screenWidth : params.right;
			bottom = (params.bottom == 0 || params.bottom == -1) ? screenHeight : params.bottom;
		}
		else if (params.left == 0 && params.top == 0 && params.right == 0 && params.bottom == 0) {
			// FIX: When user passes (0,0,0,0) for monitor/virtual desktop, capture full area
			left = screenLeft;
			top = screenTop;
			right = screenRight;
			bottom = screenBottom;
		}
		else {
			// iScreen > 0 or < 0: Apply clamping with screen bounds
			left = std::clamp(params.left, screenLeft, screenRight - 1);
			top = std::clamp(params.top, screenTop, screenBottom - 1);
			right = (params.right <= left || params.right > screenRight) ? screenRight : params.right;
			bottom = (params.bottom <= top || params.bottom > screenBottom) ? screenBottom : params.bottom;
		}

		capture_left = left;
		capture_top = top;
		capture_right = right;
		capture_bottom = bottom;
		capture_width = capture_right - capture_left;
		capture_height = capture_bottom - capture_top;

		if (left >= right || top >= bottom) {
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			result_stream << FormatError(ErrorCode::InvalidSearchRegion);

			if (params.return_debug > 0) {
				result_stream << L"(time=" << duration << L"ms"
					<< L", params=left:" << left << L",top:" << top << L",right:" << right << L",bottom:" << bottom
					<< L",screen:" << params.screen 
					<< L",use_cache=" + std::to_wstring(params.use_cache)
					<< L",tolerance:" << params.tolerance << L",max_results:" << params.max_results
					<< L",center_pos:" << params.center_pos << L",min_scale:" << FormatFloat(params.min_scale)
					<< L",max_scale:" << FormatFloat(params.max_scale) << L",scale_step:" << FormatFloat(params.scale_step)
					<< L",mode:" << (int)params.mode << L")";
			}
			return result_stream.str();
		}

		Source_opt = CaptureScreen_GDI(capture_left, capture_top, capture_right, capture_bottom, params.screen);
		search_offset_x = capture_left;
		search_offset_y = capture_top;
		Source_source = L"Screen";

	}
	else if (params.mode == SearchMode::SearchImageInImage) {
		if (!params.source_image || wcslen(params.source_image) == 0) {
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			result_stream << FormatError(ErrorCode::InvalidParameters);

			if (params.return_debug > 0) {
				result_stream << L"(time=" << duration << L"ms"
					<< L", params=source_image:" << (params.source_image ? params.source_image : L"null")
					<< L",use_cache=" + std::to_wstring(params.use_cache)
					<< L",tolerance:" << params.tolerance << L",max_results:" << params.max_results
					<< L",center_pos:" << params.center_pos << L",min_scale:" << FormatFloat(params.min_scale)
					<< L",max_scale:" << FormatFloat(params.max_scale) << L",scale_step:" << FormatFloat(params.scale_step)
					<< L",mode:" << (int)params.mode << L")";
			}
			return result_stream.str();
		}

		Source_opt = LoadImageFromFile_GDI(params.source_image);
		Source_source = params.source_image;

	}
	else {
		if (!params.Source_hbitmap) {
			auto end_time = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
			result_stream << FormatError(ErrorCode::InvalidSourceBitmap);

			if (params.return_debug > 0) {
				result_stream << L"(time=" << duration << L"ms"
					<< L", params=Source_hbitmap:" << (params.Source_hbitmap ? L"valid" : L"null")
					<< L",Target_hbitmap:" << (params.Target_hbitmap ? L"valid" : L"null")
					<< L",use_cache=" + std::to_wstring(params.use_cache)
					<< L",tolerance:" << params.tolerance << L",max_results:" << params.max_results
					<< L",center_pos:" << params.center_pos << L",min_scale:" << FormatFloat(params.min_scale)
					<< L",max_scale:" << FormatFloat(params.max_scale) << L",scale_step:" << FormatFloat(params.scale_step)
					<< L",mode:" << (int)params.mode << L")";
			}
			return result_stream.str();
		}

		Source_opt = GetBitmapPixels_GDI(params.Source_hbitmap);

		if (params.left != 0 || params.top != 0 || params.right != 0 || params.bottom != 0) {
			if (Source_opt && Source_opt->IsValid()) {
				int left = std::clamp(params.left, 0, Source_opt->width - 1);
				int top = std::clamp(params.top, 0, Source_opt->height - 1);
				int right = (params.right <= left || params.right > Source_opt->width) ? Source_opt->width : params.right;
				int bottom = (params.bottom <= top || params.bottom > Source_opt->height) ? Source_opt->height : params.bottom;

				if (left < right && top < bottom) {
					PixelBuffer cropped;
					cropped.width = right - left;
					cropped.height = bottom - top;
					cropped.has_alpha = Source_opt->has_alpha;
					cropped.pixels = g_pixel_pool.Acquire(cropped.width * cropped.height);
					cropped.pixels.resize(cropped.width * cropped.height);

					for (int y = 0; y < cropped.height; ++y) {
						const COLORREF* src_row = &Source_opt->pixels[(top + y) * Source_opt->width + left];
						COLORREF* dst_row = &cropped.pixels[y * cropped.width];
						memcpy(dst_row, src_row, cropped.width * sizeof(COLORREF));
					}

					Source_opt = std::move(cropped);
					search_offset_x = left;
					search_offset_y = top;
				}
			}
		}
		Source_source = L"HBITMAP";
	}

	if (!Source_opt || !Source_opt->IsValid()) {
		auto end_time = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
		result_stream << FormatError(ErrorCode::FailedToGetScreenDC);

		if (params.return_debug > 0) {
			result_stream << L"(time=" << duration << L"ms"
				<< L", params=left:" << params.left << L",top:" << params.top << L",right:" << params.right << L",bottom:" << params.bottom
				<< L",screen:" << params.screen
				<< L",use_cache:" << params.use_cache
				<< L",tolerance:" << params.tolerance << L",max_results:" << params.max_results
				<< L",center_pos:" << params.center_pos << L",min_scale:" << FormatFloat(params.min_scale)
				<< L",max_scale:" << FormatFloat(params.max_scale) << L",scale_step:" << FormatFloat(params.scale_step)
				<< L",mode:" << (int)params.mode
				<< L",Source_valid:" << (Source_opt ? L"yes" : L"no")
				<< L")";
		}
		return result_stream.str();
	}

	const PixelBuffer& Source = *Source_opt;

	const wchar_t* target_file_list = nullptr;
	if (params.mode == SearchMode::ScreenSearch) {
		target_file_list = params.image_files;
	}
	else if (params.mode == SearchMode::SearchImageInImage) {
		target_file_list = params.target_images;
	}

	std::vector<std::wstring> target_files;

	if (params.mode == SearchMode::HBitmapSearch) {
		target_files.push_back(L"HBITMAP");
	}
	else if (target_file_list && wcslen(target_file_list) > 0) {
		std::wstring_view all_files(target_file_list);
		size_t start_pos = 0;
		while (start_pos < all_files.length()) {
			size_t end_pos = all_files.find(L'|', start_pos);
			if (end_pos == std::wstring_view::npos) {
				end_pos = all_files.length();
			}

			std::wstring_view file_view = all_files.substr(start_pos, end_pos - start_pos);
			if (!file_view.empty()) {
				target_files.emplace_back(file_view);
			}

			if (end_pos == all_files.length()) break;
			start_pos = end_pos + 1;
		}
	}

	if (target_files.empty()) {
		auto end_time = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
		result_stream << FormatError(ErrorCode::InvalidParameters);

		if (params.return_debug > 0) {
			result_stream << L"(time=" << duration << L"ms"
				<< L", params=target_files:" << target_files.size()
				<< L",tolerance:" << params.tolerance << L",max_results:" << params.max_results
				<< L",center_pos:" << params.center_pos << L",min_scale:" << FormatFloat(params.min_scale)
				<< L",max_scale:" << FormatFloat(params.max_scale) << L",scale_step:" << FormatFloat(params.scale_step)
				<< L",mode:" << (int)params.mode << L")";
		}
		return result_stream.str();
	}

	std::vector<std::future<std::optional<PixelBuffer>>> load_futures;

	if (params.mode == SearchMode::HBitmapSearch) {
		if (params.Target_hbitmap) {
			load_futures.push_back(std::async(std::launch::deferred, [&]() {
				return GetBitmapPixels_GDI(params.Target_hbitmap);
				}));
		}
	}
	else if (target_files.size() > 1) {
		for (const auto& file : target_files) {
			load_futures.push_back(std::async(std::launch::async, [file]() {
				return LoadImageFromFile_GDI(file);
				}));
		}
	}
	else {
		load_futures.push_back(std::async(std::launch::deferred, [&]() {
			return LoadImageFromFile_GDI(target_files[0]);
			}));
	}

	std::vector<MatchResult> all_matches;
	bool find_all = (params.max_results >= 2);
	int cache_hits = 0, cache_misses = 0;
	std::wstring backend_used;

	bool skip_scaling = (std::abs(min_scale - 1.0f) < 0.001f && std::abs(max_scale - 1.0f) < 0.001f);

	for (size_t i = 0; i < load_futures.size(); ++i) {
		auto Target_opt = load_futures[i].get();
		if (!Target_opt || !Target_opt->IsValid()) continue;

		const PixelBuffer& Target = *Target_opt;
		bool transparent_enabled = Target.has_alpha;
		std::wstring source_file = (i < target_files.size()) ? target_files[i] : L"";

		std::vector<MatchResult> current_file_matches;

		if (skip_scaling) {
			std::wstring cache_key;
			if (!source_file.empty() && params.use_cache) {
				cache_key = GenerateCacheKey(Source_source, source_file, tolerance, transparent_enabled, 1.0f);
				// Load cache from disk if not already in memory
				if (!GetCachedLocation(cache_key).has_value()) {
					LoadCacheForImage(cache_key);
				}
			}

			bool found_in_cache = false;

			if (!cache_key.empty() && params.use_cache) {
				auto cached_entry = GetCachedLocation(cache_key);
				if (cached_entry) {
					// Validate cached position is within current search region
					int cached_abs_x = cached_entry->position.x;
					int cached_abs_y = cached_entry->position.y;

					// Check if cached position is within the actual capture bounds
					if (cached_abs_x >= capture_left &&
						cached_abs_y >= capture_top &&
						cached_abs_x + Target.width <= capture_right &&
						cached_abs_y + Target.height <= capture_bottom) {

						int check_x = cached_abs_x - search_offset_x;
						int check_y = cached_abs_y - search_offset_y;

						if (check_x >= 0 && check_y >= 0 &&
							check_x + Target.width <= Source.width &&
							check_y + Target.height <= Source.height) {

							bool found_at_cache = PixelComparison::CheckApproxMatch_Scalar(
								Source, Target, check_x, check_y, transparent_enabled, tolerance);

							if (found_at_cache) {
								current_file_matches.push_back(MatchResult(cached_entry->position.x, cached_entry->position.y,
									Target.width, Target.height, 1.0f, source_file));
								found_in_cache = true;
								cache_hits++;

								CacheEntry updated = *cached_entry;
								updated.miss_count = 0;
								UpdateCachedLocation(cache_key, updated);
							}
							else {
								cache_misses++;
								CacheEntry updated = *cached_entry;
								updated.miss_count++;
								if (updated.miss_count >= CACHE_MISS_THRESHOLD) {
									RemoveFromCache(cache_key);
								}
								else {
									UpdateCachedLocation(cache_key, updated);
								}
							}
						}
					}
					// else: Cached position is outside current search region - skip cache
				}
			}

			// Perform full search if not found in cache or if finding all matches
			// CRITICAL FIX: Always add matches to results, regardless of cache setting
			// Bug: Previously matches were only added when use_cache=1, causing search to fail when use_cache=0
			if (!found_in_cache || find_all) {
				auto matches = SearchForBitmap(Source, Target, search_offset_x, search_offset_y,
					tolerance, transparent_enabled, find_all, 1.0f, source_file, backend_used);

				// Always add matches to results, regardless of cache setting
				if (!matches.empty()) {
					current_file_matches.insert(current_file_matches.end(), matches.begin(), matches.end());

					// Save to cache only if caching is enabled
					if (params.use_cache && !cache_key.empty()) {
						POINT new_pos = { matches[0].x, matches[0].y };
						CacheEntry entry;
						entry.position = new_pos;
						entry.miss_count = 0;
						entry.last_used = std::chrono::steady_clock::now();
						UpdateCachedLocation(cache_key, entry);
						SaveCacheForImage(cache_key, new_pos);
					}
				}
			}
		}
		else {
			std::vector<float> scales;
			for (float scale = min_scale; scale <= max_scale; scale += scale_step) {
				scales.push_back(std::round(scale * 10.0f) / 10.0f);
			}

			if (find_all && scales.size() > 1) {
				std::vector<std::future<std::vector<MatchResult>>> scale_futures;

				for (float scale : scales) {
					scale_futures.push_back(std::async(std::launch::async, [&, scale]() {
						std::vector<MatchResult> scale_matches;

						int newW = static_cast<int>(std::round(Target.width * scale));
						int newH = static_cast<int>(std::round(Target.height * scale));
						if (newW <= 0 || newH <= 0 || newW > Source.width || newH > Source.height) {
							return scale_matches;
						}

						auto scaled_opt = ScaleBitmap_GDI(Target, newW, newH);
						if (scaled_opt && scaled_opt->IsValid()) {
							std::wstring thread_backend;
							scale_matches = SearchForBitmap(Source, *scaled_opt, search_offset_x, search_offset_y,
								tolerance, transparent_enabled, true, scale, source_file, thread_backend);
						}
						return scale_matches;
						}));
				}

				for (auto& fut : scale_futures) {
					auto scale_results = fut.get();
					if (!scale_results.empty()) {
						current_file_matches.insert(current_file_matches.end(), scale_results.begin(), scale_results.end());
					}
				}

			}
			else {
				for (float scale : scales) {
					int newW = static_cast<int>(std::round(Target.width * scale));
					int newH = static_cast<int>(std::round(Target.height * scale));
					if (newW <= 0 || newH <= 0 || newW > Source.width || newH > Source.height) {
						continue;
					}

					auto scaled_opt = ScaleBitmap_GDI(Target, newW, newH);
					if (scaled_opt && scaled_opt->IsValid()) {
						auto matches = SearchForBitmap(Source, *scaled_opt, search_offset_x, search_offset_y,
							tolerance, transparent_enabled, find_all, scale, source_file, backend_used);
						if (!matches.empty()) {
							current_file_matches.insert(current_file_matches.end(), matches.begin(), matches.end());
							if (!find_all) break;
						}
					}
				}
			}
		}

		if (!current_file_matches.empty()) {
			all_matches.insert(all_matches.end(), current_file_matches.begin(), current_file_matches.end());
			if (!find_all) break;
		}
	}

	auto end_time = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

	size_t match_count = all_matches.size();
	if (params.max_results > 0 && match_count > static_cast<size_t>(params.max_results)) {
		match_count = params.max_results;
	}

	if (match_count > 0) {
		std::wstringstream matches_stream;
		for (size_t i = 0; i < match_count; ++i) {
			if (i > 0) matches_stream << L",";
			int x = all_matches[i].x;
			int y = all_matches[i].y;
			if (params.center_pos == 1) {
				x += all_matches[i].w / 2;
				y += all_matches[i].h / 2;
			}
			matches_stream << x << L"|" << y << L"|" << all_matches[i].w << L"|" << all_matches[i].h;
		}
		result_stream << L"{" << match_count << L"}[" << matches_stream.str() << L"]";
	}
	else {
		result_stream << L"{0}[]";
	}

	std::wstring debug_info;
	if (params.return_debug > 0) {
		debug_info = L"(time=" + std::to_wstring(duration) + L"ms"
			+ L", backend=" + backend_used
			+ L", Source=" + std::to_wstring(Source.width) + L"x" + std::to_wstring(Source.height)
			+ L", files=" + std::to_wstring(target_files.size())
			+ L", cache_hits=" + std::to_wstring(cache_hits)
			+ L", cache_misses=" + std::to_wstring(cache_misses)
			+ L", tolerance=" + std::to_wstring(tolerance)
			+ L", scale=" + FormatFloat(min_scale) + L"-" + FormatFloat(max_scale) + L":" + FormatFloat(scale_step)
#ifdef _WIN64
			+ L", cpu=AVX2:" + (g_is_avx2_supported.load() ? L"Y" : L"N")
			+ L"/AVX512:" + (g_is_avx512_supported.load() ? L"Y" : L"N")
#else
			+ L", cpu=SSE2:" + (g_is_sse2_supported.load() ? L"Y" : L"N")
#endif
			+ L", capture=" + std::to_wstring(capture_left) + L"|" + std::to_wstring(capture_top) + L"|" + std::to_wstring(capture_right) + L"|" + std::to_wstring(capture_bottom)
			+ L"|" + std::to_wstring(capture_width) + L"x" + std::to_wstring(capture_height)
			+ L", screen=" + std::to_wstring(params.screen)
			+ L")";
	}

	if (!debug_info.empty()) {
		result_stream << debug_info;
	}

	return result_stream.str();
}

// Helper: Check and handle result buffer overflow
static void CheckResultBufferSize(std::wstring& result_buffer, const SearchParams& params) {
	if (result_buffer.length() >= MAX_RESULT_STRING_LENGTH) {
		result_buffer = FormatError(ErrorCode::ResultTooLarge);

		if (params.return_debug > 0) {
			result_buffer += L"(buffer_size=" + std::to_wstring(result_buffer.length())
				+ L",params=left:" + std::to_wstring(params.left) + L",top:" + std::to_wstring(params.top)
				+ L",right:" + std::to_wstring(params.right) + L",bottom:" + std::to_wstring(params.bottom)
				+ L",screen:" + std::to_wstring(params.screen)
				+ L",tolerance:" + std::to_wstring(params.tolerance) + L",max_results:" + std::to_wstring(params.max_results)
				+ L",center_pos:" + std::to_wstring(params.center_pos) + L",min_scale:" + FormatFloat(params.min_scale)
				+ L",max_scale:" + FormatFloat(params.max_scale) + L",scale_step:" + FormatFloat(params.scale_step)
				+ L",mode:" + std::to_wstring((int)params.mode) + L")";
		}
	}
}

// Helper function to convert screen coordinates to absolute coordinates
static void ScreenToAbsolute(int x, int y, LONG& ax, LONG& ay) {
	LONG64 screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	LONG64 screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	//int screenWidth = GetSystemMetrics(SM_CXSCREEN);
   // int screenHeight = GetSystemMetrics(SM_CYSCREEN);
	if (screenWidth <= 0)  screenWidth = 1;
	if (screenHeight <= 0) screenHeight = 1;

	// Tính toán với LONG64 rồi mới cast
	LONG64 ax_temp = (static_cast<LONG64>(x) * 65535LL) / screenWidth;
	LONG64 ay_temp = (static_cast<LONG64>(y) * 65535LL) / screenHeight;

	// Clamp trước khi cast về LONG
	ax = static_cast<LONG>(std::clamp(ax_temp, 0LL, 65535LL));
	ay = static_cast<LONG>(std::clamp(ay_temp, 0LL, 65535LL));
}

// Helper function to get mouse button flags
static void GetMouseButtonFlags(const wchar_t* button, DWORD& downFlag, DWORD& upFlag) {
	std::wstring btn = button;
	for (size_t i = 0; i < btn.length(); ++i) {
		btn[i] = towlower(btn[i]);
	}

	if (btn == L"left" || btn == L"main" || btn == L"primary") {
		downFlag = MOUSEEVENTF_LEFTDOWN;
		upFlag = MOUSEEVENTF_LEFTUP;
	}
	else if (btn == L"right" || btn == L"menu" || btn == L"secondary") {
		downFlag = MOUSEEVENTF_RIGHTDOWN;
		upFlag = MOUSEEVENTF_RIGHTUP;
	}
	else if (btn == L"middle") {
		downFlag = MOUSEEVENTF_MIDDLEDOWN;
		upFlag = MOUSEEVENTF_MIDDLEUP;
	}
	else {
		downFlag = MOUSEEVENTF_LEFTDOWN;
		upFlag = MOUSEEVENTF_LEFTUP;
	}
}

// Helper function to move mouse smoothly
static void SmoothMouseMove(int startX, int startY, int endX, int endY, int speed) {
	if (speed == 0) {
		SetCursorPos(endX, endY);
		return;
	}

	// Speed: 1 = fastest (few steps), 100 = slowest (many steps)
	int steps = (speed > 1) ? speed : 1;
	int deltaX = endX - startX;
	int deltaY = endY - startY;
	double distance = std::sqrt(static_cast<double>(deltaX * deltaX + deltaY * deltaY));

	if (distance < 1.0) {
		SetCursorPos(endX, endY);
		return;
	}

	// Calculate delay between steps based on speed
	int totalDelay = speed * 2; // Total time in ms
	int delayPerStep = totalDelay / steps;
	if (delayPerStep < 1) delayPerStep = 1;

	for (int i = 1; i <= steps; i++) {
		int x = startX + (deltaX * i) / steps;
		int y = startY + (deltaY * i) / steps;
		SetCursorPos(x, y);
		Sleep(delayPerStep);
	}
}

// Fallback: Click using mouse_event
static void PerformClick_MouseEvent(const wchar_t* button, int clicks) {
	DWORD downFlag = 0, upFlag = 0;
	GetMouseButtonFlags(button, downFlag, upFlag);

	for (int i = 0; i < clicks; i++) {
		mouse_event(downFlag, 0, 0, 0, 0);
		Sleep(10);
		mouse_event(upFlag, 0, 0, 0, 0);
		if (i < clicks - 1) {
			Sleep(50);
		}
	}
}

// Enhanced click function using SendInput with fallback to mouse_event
static bool PerformClick(int x, int y, const wchar_t* button, int clicks, int speed, bool restorePosition) {
	POINT currentPos;
	GetCursorPos(&currentPos);

	DWORD downFlag = 0, upFlag = 0;
	GetMouseButtonFlags(button, downFlag, upFlag);

	bool useFallback = false;

	// Move to position if needed
	if (speed == 0) {
		// Instant move using SendInput with absolute coordinates
		LONG ax = 0, ay = 0;
		ScreenToAbsolute(x, y, ax, ay);

		INPUT moveInput;
		ZeroMemory(&moveInput, sizeof(INPUT));
		moveInput.type = INPUT_MOUSE;
		moveInput.mi.dx = ax;
		moveInput.mi.dy = ay;
		moveInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
		moveInput.mi.dwExtraInfo = 0;

		if (SendInput(1, &moveInput, sizeof(INPUT)) != 1) {
			// Fallback to SetCursorPos
			SetCursorPos(x, y);
			useFallback = true;
		}
		Sleep(5);
	}
	else {
		// Smooth move
		SmoothMouseMove(currentPos.x, currentPos.y, x, y, speed);
	}

	// Perform clicks
	if (!useFallback) {
		for (int i = 0; i < clicks; i++) {
			INPUT inputs[2];
			ZeroMemory(inputs, sizeof(inputs));

			// Mouse down
			inputs[0].type = INPUT_MOUSE;
			inputs[0].mi.dwFlags = downFlag;
			inputs[0].mi.dwExtraInfo = 0;

			// Mouse up
			inputs[1].type = INPUT_MOUSE;
			inputs[1].mi.dwFlags = upFlag;
			inputs[1].mi.dwExtraInfo = 0;

			// Send down
			if (SendInput(1, &inputs[0], sizeof(INPUT)) != 1) {
				// Fallback to mouse_event for remaining clicks
				useFallback = true;
				PerformClick_MouseEvent(button, clicks - i);
				break;
			}
			Sleep(10);

			// Send up
			if (SendInput(1, &inputs[1], sizeof(INPUT)) != 1) {
				// Fallback to mouse_event for remaining clicks
				useFallback = true;
				// Already sent down, so just send up and continue
				mouse_event(upFlag, 0, 0, 0, 0);
				if (i < clicks - 1) {
					Sleep(50);
					PerformClick_MouseEvent(button, clicks - i - 1);
				}
				break;
			}

			if (i < clicks - 1) {
				Sleep(50);
			}
		}
	}
	else {
		// Use mouse_event fallback
		PerformClick_MouseEvent(button, clicks);
	}

	// Restore position if needed
	if (restorePosition) {
		Sleep(10);

		LONG ax = 0, ay = 0;
		ScreenToAbsolute(currentPos.x, currentPos.y, ax, ay);

		INPUT moveInput;
		ZeroMemory(&moveInput, sizeof(INPUT));
		moveInput.type = INPUT_MOUSE;
		moveInput.mi.dx = ax;
		moveInput.mi.dy = ay;
		moveInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
		moveInput.mi.dwExtraInfo = 0;

		if (SendInput(1, &moveInput, sizeof(INPUT)) != 1) {
			// Fallback to SetCursorPos
			SetCursorPos(currentPos.x, currentPos.y);
		}
	}

	return true;
}

// Helper structure for text search in child windows
struct TextSearchData {
	const wchar_t* text;
	bool found;

	TextSearchData() : text(NULL), found(false) {}
};

// Callback for EnumChildWindows in text search
static BOOL CALLBACK EnumChildWindowsTextProc(HWND hwndChild, LPARAM lParam) {
	TextSearchData* data = reinterpret_cast<TextSearchData*>(lParam);
	wchar_t controlText[1024];
	memset(controlText, 0, sizeof(controlText));
	GetWindowTextW(hwndChild, controlText, 1024);
	if (wcsstr(controlText, data->text) != NULL) {
		data->found = true;
		return FALSE; // Found, stop enumeration
	}
	return TRUE;
}

// Helper function to check if window/controls contain text
static bool WindowContainsText(HWND hWnd, const wchar_t* searchText) {
	if (!searchText || wcslen(searchText) == 0) {
		return true; // No text to search, consider as match
	}

	// Check window itself first (fastest)
	wchar_t windowText[1024];
	memset(windowText, 0, sizeof(windowText));
	GetWindowTextW(hWnd, windowText, 1024);
	if (wcsstr(windowText, searchText) != NULL) {
		return true;
	}

	// Check in child controls
	TextSearchData searchData;
	searchData.text = searchText;
	searchData.found = false;

	EnumChildWindows(hWnd, EnumChildWindowsTextProc, reinterpret_cast<LPARAM>(&searchData));

	return searchData.found;
}

// Helper structure for window candidate
struct WindowCandidate {
	HWND hwnd;
	std::wstring title;
	int titleMatchQuality; // 0=exact, 1=starts_with, 2=contains

	WindowCandidate() : hwnd(NULL), title(L""), titleMatchQuality(0) {}
};

// Helper structure for window enumeration
struct WindowEnumData {
	const wchar_t* searchTitle;
	std::vector<WindowCandidate>* candidatesList;

	WindowEnumData() : searchTitle(NULL), candidatesList(NULL) {}
};

// Callback for EnumWindows
static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
	WindowEnumData* data = reinterpret_cast<WindowEnumData*>(lParam);
	const wchar_t* searchTitle = data->searchTitle;
	std::vector<WindowCandidate>* candidatesList = data->candidatesList;

	// Only check visible windows
	if (!IsWindowVisible(hwnd)) {
		return TRUE;
	}

	wchar_t windowTitle[1024];
	memset(windowTitle, 0, sizeof(windowTitle));
	int titleLen = GetWindowTextW(hwnd, windowTitle, 1024);

	if (titleLen == 0) {
		return TRUE;
	}

	// Check if title matches (partial substring)
	const wchar_t* matchPos = wcsstr(windowTitle, searchTitle);
	if (matchPos != NULL) {
		WindowCandidate candidate;
		candidate.hwnd = hwnd;
		candidate.title = windowTitle;

		// Determine match quality
		if (wcscmp(windowTitle, searchTitle) == 0) {
			candidate.titleMatchQuality = 0; // Exact match
		}
		else if (matchPos == windowTitle) {
			candidate.titleMatchQuality = 1; // Starts with
		}
		else {
			candidate.titleMatchQuality = 2; // Contains
		}

		candidatesList->push_back(candidate);
	}

	return TRUE; // Continue enumeration
}

// Comparison function for sorting candidates
static bool CompareWindowCandidates(const WindowCandidate& a, const WindowCandidate& b) {
	return a.titleMatchQuality < b.titleMatchQuality;
}

// Helper function to find window by title, text, or handle
static HWND FindTargetWindow(const wchar_t* title, const wchar_t* text) {
	if (!title || wcslen(title) == 0) return NULL;

	HWND hWnd = NULL;

	// Try to parse as HWND (hexadecimal or decimal)
	if (wcsncmp(title, L"0x", 2) == 0) {
		hWnd = reinterpret_cast<HWND>(_wcstoui64(title, NULL, 16));
	}
	else if (iswdigit(title[0])) {
		hWnd = reinterpret_cast<HWND>(_wcstoui64(title, NULL, 10));
	}

	// Validate if parsed as HWND and check if visible
	if (hWnd && IsWindow(hWnd) && IsWindowVisible(hWnd)) {
		if (WindowContainsText(hWnd, text)) {
			return hWnd;
		}
		return NULL;
	}

	// Step 1: Try exact title match first (fastest)
	hWnd = FindWindowW(NULL, title);
	if (hWnd && IsWindow(hWnd) && IsWindowVisible(hWnd)) {
		if (WindowContainsText(hWnd, text)) {
			return hWnd; // Found with exact title and text match
		}
	}

	// Step 2: Try exact class name match
	hWnd = FindWindowW(title, NULL);
	if (hWnd && IsWindow(hWnd) && IsWindowVisible(hWnd)) {
		if (WindowContainsText(hWnd, text)) {
			return hWnd; // Found with exact class and text match
		}
	}

	// Step 3: Collect all windows with partial title match
	std::vector<WindowCandidate> candidates;

	WindowEnumData enumData;
	enumData.searchTitle = title;
	enumData.candidatesList = &candidates;

	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&enumData));

	// If no candidates found, return NULL
	if (candidates.empty()) {
		return NULL;
	}

	// Step 4: Sort candidates by match quality (best matches first)
	std::sort(candidates.begin(), candidates.end(), CompareWindowCandidates);

	// Step 5: Check text in candidates (best matches first)
	for (size_t i = 0; i < candidates.size(); ++i) {
		if (WindowContainsText(candidates[i].hwnd, text)) {
			return candidates[i].hwnd; // Found first window with title and text match
		}
	}

	// No window found with both title and text match
	return NULL;
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch_MouseClick
// ============================================================================
// Description:
//   Simulates mouse click at specified coordinates with configurable parameters.
//   Supports smooth mouse movement and multiple click modes.
//
// Parameters:
//   sButton  - Mouse button to click:
//              * "left", "main", "primary" = Left mouse button
//              * "right", "menu", "secondary" = Right mouse button
//              * "middle" = Middle mouse button
//   iX       - X coordinate (-1 = current position, no movement)
//   iY       - Y coordinate (-1 = current position, no movement)
//   iClicks  - Number of clicks to perform (1 = single, 2 = double, etc.)
//   iSpeed   - Mouse movement speed (0-100):
//              * 0   = Instant teleport (no smooth movement)
//              * 1   = Fastest smooth movement
//              * 100 = Slowest smooth movement
//   iScreen  - Coordinate system (same as ImageSearch):
//              * 0  = Absolute screen coordinates
//              * >0 = Relative to specific monitor (1-based)
//              * <0 = Relative to virtual screen
//
// Returns:
//   1 on success, 0 on failure
//
// Behavior:
//   - When iSpeed = 0 and coordinates provided, cursor position is restored after click
//   - Uses SendInput API with fallback to mouse_event for compatibility
//   - Supports multi-monitor setups with proper coordinate translation
//
// Example:
//   // Double-click at (100, 200) on primary monitor with smooth movement
//   ImageSearch_MouseClick(L"left", 100, 200, 2, 50, 0);
// ============================================================================
extern "C" __declspec(dllexport) int WINAPI ImageSearch_MouseClick(
	const wchar_t* sButton,
	int iX,
	int iY,
	int iClicks,
	int iSpeed,
	int iScreen
) {
	if (!sButton) return 0;

	if (iClicks < 1) iClicks = 1;
	if (iSpeed < 0) iSpeed = 0;
	if (iSpeed > 100) iSpeed = 100;

	POINT currentPos;
	GetCursorPos(&currentPos);

	bool needMove = (iX != -1 && iY != -1);
	bool restorePosition = (iSpeed == 0 && needMove);

	int targetX = currentPos.x;
	int targetY = currentPos.y;

	if (needMove) {
		if (iScreen > 0) {
			// iScreen > 0: Coordinates relative to specific monitor
			RECT monitorBounds;
			if (GetMonitorBounds(iScreen, monitorBounds)) {
				targetX = monitorBounds.left + iX;
				targetY = monitorBounds.top + iY;
			}
			else {
				// Fallback if monitor bounds unavailable
				targetX = iX;
				targetY = iY;
			}
		}
		else {
			// iScreen <= 0: Coordinates are already absolute (no offset needed)
			// FIX: DLL returns absolute virtual coordinates, don't add offset again
			targetX = iX;
			targetY = iY;
		}

		PerformClick(targetX, targetY, sButton, iClicks, iSpeed, restorePosition);
	}
	else {
		PerformClick(currentPos.x, currentPos.y, sButton, iClicks, iSpeed, false);
	}

	return 1;
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch_MouseMove
// ============================================================================
// Description:
//   Moves mouse cursor to specified screen coordinates with optional smooth movement.
//   Supports multi-monitor setups and virtual desktop coordinates (including negative).
//
// Parameters:
//   iX, iY   - Target coordinates (-1 = keep current position)
//   iSpeed   - Movement speed 0-100 (0=instant, 100=slowest)
//   iScreen  - Monitor selection:
//              * 0  = Primary monitor coordinates (default)
//              * >0 = Relative to specific monitor (1-based)
//              * <0 = Relative to virtual screen (supports negative coords)
//
// Returns:
//   1 on success, 0 on failure
//
// Example:
//   // Move to (-274, -416) on virtual desktop instantly
//   ImageSearch_MouseMove(-274, -416, 0, -1);
// ============================================================================
extern "C" __declspec(dllexport) int WINAPI ImageSearch_MouseMove(
	int iX,
	int iY,
	int iSpeed,
	int iScreen
) {
	if (iSpeed < 0) iSpeed = 0;
	if (iSpeed > 100) iSpeed = 100;

	POINT currentPos;
	GetCursorPos(&currentPos);

	// If no coordinates specified, nothing to do
	if (iX == -1 && iY == -1) return 1;

	int targetX = currentPos.x;
	int targetY = currentPos.y;

	// Calculate target coordinates based on iScreen
	if (iScreen > 0) {
		// iScreen > 0: Coordinates relative to specific monitor
		RECT monitorBounds;
		if (GetMonitorBounds(iScreen, monitorBounds)) {
			targetX = (iX != -1) ? (monitorBounds.left + iX) : currentPos.x;
			targetY = (iY != -1) ? (monitorBounds.top + iY) : currentPos.y;
		}
		else {
			// Fallback if monitor bounds unavailable
			targetX = (iX != -1) ? iX : currentPos.x;
			targetY = (iY != -1) ? iY : currentPos.y;
		}
	}
	else {
		// iScreen <= 0: Coordinates are already absolute (no offset needed)
		targetX = (iX != -1) ? iX : currentPos.x;
		targetY = (iY != -1) ? iY : currentPos.y;
	}

	// Perform move
	if (iSpeed == 0) {
		// Instant move using SendInput with absolute coordinates
		LONG ax = 0, ay = 0;
		ScreenToAbsolute(targetX, targetY, ax, ay);

		INPUT moveInput;
		ZeroMemory(&moveInput, sizeof(INPUT));
		moveInput.type = INPUT_MOUSE;
		moveInput.mi.dx = ax;
		moveInput.mi.dy = ay;
		moveInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
		moveInput.mi.dwExtraInfo = 0;

		if (SendInput(1, &moveInput, sizeof(INPUT)) != 1) {
			// Fallback to SetCursorPos
			SetCursorPos(targetX, targetY);
		}
	}
	else {
		// Smooth move
		SmoothMouseMove(currentPos.x, currentPos.y, targetX, targetY, iSpeed);
	}

	return 1;
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch_MouseClickWin
// ============================================================================
// Description:
//   Clicks at coordinates relative to a specific window.
//   Finds window by title/class and optionally validates by text content.
//
// Parameters:
//   sTitle  - Window title, class name, or HWND (as decimal/hex string):
//             * Window title (partial match supported)
//             * Window class name
//             * "0x123ABC" or "123456" = Direct HWND handle
//   sText   - Optional text that must exist in window/controls (empty = any)
//   iX      - X coordinate relative to window's top-left corner
//   iY      - Y coordinate relative to window's top-left corner
//   sButton - Mouse button ("left", "right", "middle")
//   iClicks - Number of clicks (1, 2, 3, ...)
//   iSpeed  - Movement speed (0-100, same as ImageSearch_MouseClick)
//
// Returns:
//   1 on success, 0 on failure (window not found, coordinates out of bounds)
//
// Window Search Priority:
//   1. Try exact title match
//   2. Try exact class name match
//   3. Enumerate all windows for partial title match
//   4. Best match is selected (exact > starts_with > contains)
//   5. Validate found window contains sText (if specified)
//
// Coordinate Validation:
//   Click coordinates must be within window bounds, otherwise function fails.
//
// Example:
//   // Click "OK" button in "Settings" window at position (100, 50)
//   ImageSearch_MouseClickWin(L"Settings", L"OK", 100, 50, L"left", 1, 0);
// ============================================================================
extern "C" __declspec(dllexport) int WINAPI ImageSearch_MouseClickWin(
	const wchar_t* sTitle,
	const wchar_t* sText,
	int iX,
	int iY,
	const wchar_t* sButton,
	int iClicks,
	int iSpeed
) {
	if (!sTitle || wcslen(sTitle) == 0) return 0;
	if (!sButton) sButton = L"left";

	// Find target window
	HWND hWnd = FindTargetWindow(sTitle, sText);
	if (!hWnd || !IsWindow(hWnd)) return 0;

	// Get window rectangle
	RECT rect;
	if (!GetWindowRect(hWnd, &rect)) return 0;

	// Calculate absolute screen coordinates
	int screenX = rect.left + iX;
	int screenY = rect.top + iY;

	// Validate coordinates are within window bounds
	if (screenX < rect.left || screenX > rect.right ||
		screenY < rect.top || screenY > rect.bottom) {
		return 0; // Click position outside window
	}

	// Clamp parameters
	if (iClicks < 1) iClicks = 1;
	if (iSpeed < 0) iSpeed = 0;
	if (iSpeed > 100) iSpeed = 100;

	bool restorePosition = (iSpeed == 0);

	// Perform click with SendInput (auto-fallback to mouse_event if fail)
	PerformClick(screenX, screenY, sButton, iClicks, iSpeed, restorePosition);

	return 1;
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch
// ============================================================================
// Description:
//   Main function to search for image(s) on the screen within a specified region.
//   Supports multi-image search, scaling, transparency, and caching.
//
// Parameters:
//   sImageFile   - Pipe-separated list of image file paths to search for (e.g., "img1.png|img2.png")
//   iLeft        - Left boundary of search region (screen coordinates)
//   iTop         - Top boundary of search region (screen coordinates)
//   iRight       - Right boundary of search region (0 = screen width)
//   iBottom      - Bottom boundary of search region (0 = screen height)
//   iScreen      - Screen/monitor selection:
//                  * 0  = Use absolute coordinates (no bounds adjustment)
//                  * >0 = Specific monitor number (1-based, e.g., 1 = primary, 2 = secondary)
//                  * <0 = Virtual screen (all monitors combined)
//   iTolerance   - Color matching tolerance (0-255, default 10)
//                  * 0   = Exact match required
//                  * 255 = Maximum tolerance (any color matches)
//   iResults     - Maximum number of results to return (1 = find first, >=2 = find all)
//   iCenterPOS   - Return position mode:
//                  * 1 = Return center coordinates of found image
//                  * 0 = Return top-left coordinates
//   fMinScale    - Minimum scale factor for search (0.1-5.0, default 1.0 = 100%)
//   fMaxScale    - Maximum scale factor for search (0.1-5.0, default 1.0 = 100%)
//   fScaleStep   - Scale increment step (0.01-1.0, default 0.1 = 10%)
//   iReturnDebug - Enable debug info in result string (0 = off, 1 = on)
//   iUseCache    - Enable location caching for faster repeated searches (0 = off, 1 = on)
//
// Returns:
//   Wide string with format: "{count}[x|y|w|h,x|y|w|h,...]"
//   Examples:
//     Success:  "{1}[100|200|50|30]" - Found 1 match at (100,200) with size 50x30
//     Multiple: "{2}[100|200|50|30,150|250|50|30]" - Found 2 matches
//     No match: "{0}[]"
//     Error:    "ERROR: <error_message>"
//
// Thread Safety:
//   Thread-safe. Uses thread_local storage for result buffer.
//
// Performance:
//   - Automatically selects SIMD backend (AVX512/AVX2/SSE2/Scalar)
//   - Supports multi-threading for large images
//   - Cache system reduces search time for repeated patterns
// ============================================================================
extern "C" __declspec(dllexport) const wchar_t* WINAPI ImageSearch(
	const wchar_t* sImageFile,
	int iLeft = 0,
	int iTop = 0,
	int iRight = 0,
	int iBottom = 0,
	int iScreen = 0,
	int iTolerance = 10,
	int iResults = 1,
	int iCenterPOS = 1,
	float fMinScale = 1.0f,
	float fMaxScale = 1.0f,
	float fScaleStep = 0.1f,
	int iReturnDebug = 0,
	int iUseCache = 0
) {
	thread_local std::wstring result_buffer;

	SearchParams params;
	params.mode = SearchMode::ScreenSearch;
	params.image_files = sImageFile;
	params.left = iLeft;
	params.top = iTop;
	params.right = iRight;
	params.bottom = iBottom;
	params.screen = iScreen;
	params.tolerance = iTolerance;
	params.max_results = iResults;
	params.center_pos = iCenterPOS;
	params.min_scale = fMinScale;
	params.max_scale = fMaxScale;
	params.scale_step = fScaleStep;
	params.return_debug = iReturnDebug;
	params.use_cache = iUseCache;

	result_buffer = UnifiedImageSearch(params);

	CheckResultBufferSize(result_buffer, params);

	return result_buffer.c_str();
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch_InImage
// ============================================================================
// Description:
//   Search for target image(s) within a source image file.
//   Useful for offline image analysis without screen capture.
//
// Parameters:
//   sSourceImageFile - Path to source image file to search within
//   sTargetImageFile - Pipe-separated list of target image paths to find
//   iTolerance       - Color matching tolerance (0-255, default 10)
//   iResults         - Maximum results (1 = first match, >=2 = all matches)
//   iCenterPOS       - Position mode (1 = center, 0 = top-left)
//   fMinScale        - Minimum scale factor (0.1-5.0, default 1.0)
//   fMaxScale        - Maximum scale factor (0.1-5.0, default 1.0)
//   fScaleStep       - Scale increment (0.01-1.0, default 0.1)
//   iReturnDebug     - Debug info flag (0 = off, 1 = on)
//   iUseCache        - Caching flag (0 = off, 1 = on)
//
// Returns:
//   Same format as ImageSearch: "{count}[x|y|w|h,...]"
//   Coordinates are relative to source image origin (0,0)
//
// Use Cases:
//   - Template matching in saved screenshots
//   - Batch image processing
//   - Testing/validation without screen dependency
// ============================================================================
extern "C" __declspec(dllexport) const wchar_t* WINAPI ImageSearch_InImage(
	const wchar_t* sSourceImageFile,
	const wchar_t* sTargetImageFile,
	int iTolerance = 10,
	int iResults = 1,
	int iCenterPOS = 1,
	float fMinScale = 1.0f,
	float fMaxScale = 1.0f,
	float fScaleStep = 0.1f,
	int iReturnDebug = 0,
	int iUseCache = 0
) {
	thread_local std::wstring result_buffer;

	SearchParams params;
	params.mode = SearchMode::SearchImageInImage;
	params.source_image = sSourceImageFile;
	params.target_images = sTargetImageFile;
	params.tolerance = iTolerance;
	params.max_results = iResults;
	params.center_pos = iCenterPOS;
	params.min_scale = fMinScale;
	params.max_scale = fMaxScale;
	params.scale_step = fScaleStep;
	params.return_debug = iReturnDebug;
	params.use_cache = iUseCache;

	result_buffer = UnifiedImageSearch(params);

	CheckResultBufferSize(result_buffer, params);

	return result_buffer.c_str();
}

extern "C" __declspec(dllexport) const wchar_t* WINAPI ImageSearch_hBitmap(
	HBITMAP hBitmapSource,
	HBITMAP hBitmapTarget,
	int iTolerance,
	int iLeft,
	int iTop,
	int iRight,
	int iBottom,
	int iResults = 1,
	int iCenter = 1,
	float fMinScale = 1.0f,
	float fMaxScale = 1.0f,
	float fScaleStep = 0.1f,
	int iReturnDebug = 0,
	int iUseCache = 0
) {
	thread_local std::wstring result_buffer;

	SearchParams params;
	params.mode = SearchMode::HBitmapSearch;
	params.Source_hbitmap = hBitmapSource;
	params.Target_hbitmap = hBitmapTarget;
	params.left = iLeft;
	params.top = iTop;
	params.right = iRight;
	params.bottom = iBottom;
	params.tolerance = iTolerance;
	params.max_results = iResults;
	params.center_pos = iCenter;
	params.min_scale = fMinScale;
	params.max_scale = fMaxScale;
	params.scale_step = fScaleStep;
	params.return_debug = iReturnDebug;
	params.use_cache = iUseCache;

	result_buffer = UnifiedImageSearch(params);

	CheckResultBufferSize(result_buffer, params);

	return result_buffer.c_str();
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch_CaptureScreen
// ============================================================================
// Description:
//   Captures a screen region and returns it as an HBITMAP handle.
//   The returned bitmap can be used with ImageSearch_hBitmap or saved.
//
// Parameters:
//   iLeft   - Left boundary of capture region
//   iTop    - Top boundary of capture region
//   iRight  - Right boundary (0 = screen width)
//   iBottom - Bottom boundary (0 = screen height)
//   iScreen - Screen selection (same as ImageSearch)
//
// Returns:
//   HBITMAP handle to captured screen region, or NULL on failure
//
// IMPORTANT:
//   Caller is responsible for deleting the returned HBITMAP using DeleteObject()
//   to prevent memory leaks.
//
// Example:
//   HBITMAP hBmp = ImageSearch_CaptureScreen(0, 0, 800, 600, 0);
//   if (hBmp) {
//       // Use bitmap...
//       DeleteObject(hBmp); // Clean up!
//   }
// ============================================================================
extern "C" __declspec(dllexport) HBITMAP WINAPI ImageSearch_CaptureScreen(
	int iLeft = 0,
	int iTop = 0,
	int iRight = 0,
	int iBottom = 0,
	int iScreen = 0
) {
	InitializeGdiplus();
	return CaptureScreenInternal(iLeft, iTop, iRight, iBottom, iScreen);
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch_hBitmapLoad
// ============================================================================
// Description:
//   Loads an image file and converts it to HBITMAP with optional background color.
//   Useful for loading images to use with ImageSearch_hBitmap.
//
// Parameters:
//   sImageFile - Path to image file (supports: PNG, JPG, BMP, GIF, TIFF)
//   iAlpha     - Background alpha channel (0-255, 0 = transparent)
//   iRed       - Background red component (0-255)
//   iGreen     - Background green component (0-255)
//   iBlue      - Background blue component (0-255)
//
// Returns:
//   HBITMAP handle to loaded image, or NULL on failure
//
// Background Color Usage:
//   The ARGB values are used as background when converting the image.
//   For images with transparency, the background color fills transparent areas.
//   Default (0,0,0,0) creates a black transparent background.
//
// IMPORTANT:
//   Caller must call DeleteObject() on returned HBITMAP to prevent memory leaks.
//
// Example:
//   // Load image with white background
//   HBITMAP hBmp = ImageSearch_hBitmapLoad(L"logo.png", 255, 255, 255, 255);
//   if (hBmp) {
//       // Use bitmap...
//       DeleteObject(hBmp); // Clean up!
//   }
// ============================================================================
extern "C" __declspec(dllexport) HBITMAP WINAPI ImageSearch_hBitmapLoad(
	const wchar_t* sImageFile,
	int iAlpha = 0,
	int iRed = 0,
	int iGreen = 0,
	int iBlue = 0
) {
	if (!sImageFile || wcslen(sImageFile) == 0) {
		return nullptr;
	}

	// Check if file exists
	try {
		if (!std::filesystem::exists(sImageFile)) {
			return nullptr;
		}
	}
	catch (...) {
		return nullptr;
	}

	InitializeGdiplus();

	// Load bitmap from file
	auto bitmap = std::make_unique<Bitmap>(sImageFile);
	if (!bitmap || bitmap->GetLastStatus() != Ok) {
		return nullptr;
	}

	int width = bitmap->GetWidth();
	int height = bitmap->GetHeight();
	if (width <= 0 || height <= 0 || width > 32000 || height > 32000) {
		return nullptr;
	}

	// Clamp color values to valid range (0-255)
	int alpha = std::clamp(iAlpha, 0, 255);
	int red = std::clamp(iRed, 0, 255);
	int green = std::clamp(iGreen, 0, 255);
	int blue = std::clamp(iBlue, 0, 255);

	// Create background color from ARGB
	Color background(alpha, red, green, blue);

	// Convert to HBITMAP
	HBITMAP hBitmap = nullptr;
	if (bitmap->GetHBITMAP(background, &hBitmap) != Ok) {
		return nullptr;
	}

	return hBitmap;
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch_ClearCache
// ============================================================================
// Description:
//   Clears all cached image locations and bitmap data.
//   Deletes both in-memory cache and persistent disk cache files.
//
// When to Use:
//   - When screen content has changed significantly
//   - Before running new test scenarios
//   - To free memory when cache grows large
//   - After display resolution changes
//
// Performance Impact:
//   After clearing cache, first searches will be slower until cache rebuilds.
//   Cache automatically validates entries, so manual clearing is rarely needed.
//
// Thread Safety:
//   Thread-safe. Acquires cache mutex before clearing.
// ============================================================================
extern "C" __declspec(dllexport) void WINAPI ImageSearch_ClearCache() {
	{
		std::unique_lock lock(g_cache_mutex);
		g_location_cache_lru.clear();
		g_location_cache_index.clear();
	}

	try {
		std::wstring cache_dir = GetCacheBaseDir();
		if (!cache_dir.empty()) {
			for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
				if (entry.is_regular_file()) {
					std::wstring filename = entry.path().filename().wstring();
					if (filename.find(L"~CACHE_IMGSEARCH_") == 0 && filename.ends_with(L".dat")) {
						std::filesystem::remove(entry.path());
					}
				}
			}
		}
	}
	catch (...) {}

	{
		std::lock_guard<std::mutex> lock(g_bitmap_cache_mutex);
		g_bitmap_cache.clear();
	}
}

extern "C" __declspec(dllexport) const wchar_t* WINAPI ImageSearch_GetVersion() {
#ifdef _WIN64
	return L"ImageSearchDLL v3.3 [x64] 2025.10.15  ::  Dao Van Trong - TRONG.PRO";
#else
	return L"ImageSearchDLL v3.3 [x86] 2025.10.15  ::  Dao Van Trong - TRONG.PRO";
#endif
}

extern "C" __declspec(dllexport) const wchar_t* WINAPI ImageSearch_GetSysInfo() {
	thread_local wchar_t info_buffer[1024];
	std::call_once(g_feature_detection_flag, DetectFeatures);

	EnumerateMonitors();

	swprintf_s(info_buffer, _countof(info_buffer),
#ifdef _WIN64
		L"CPU: AVX2=%s AVX512=%s | Screen: %dx%d | Monitors=%d | LocationCache: %zu/%d | BitmapCache: %zu/%d | PoolSize: %d",
		g_is_avx2_supported.load() ? L"Yes" : L"No",
		g_is_avx512_supported.load() ? L"Yes" : L"No",
#else
		L"CPU: SSE2=%s | Screen: %dx%d | Monitors=%d | LocationCache: %zu/%d | BitmapCache: %zu/%d | PoolSize: %d",
		g_is_sse2_supported.load() ? L"Yes" : L"No",
#endif
		GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
		static_cast<int>(g_monitors.size()),
		g_location_cache_lru.size(), MAX_CACHED_LOCATIONS,
		g_bitmap_cache.size(), MAX_CACHED_BITMAPS,
		g_pixel_pool_size.load(std::memory_order_relaxed));
	return info_buffer;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
	{
		typedef BOOL(WINAPI* PFN_SetProcessDPIAware)();
		typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

		HMODULE hUser = LoadLibraryW(L"user32.dll");
		if (hUser) {
			auto pSetCtx = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
			if (pSetCtx) {
				const DPI_AWARENESS_CONTEXT ctx = (DPI_AWARENESS_CONTEXT)-4;
				pSetCtx(ctx);
			}
			else {
				auto pLegacy = (PFN_SetProcessDPIAware)GetProcAddress(hUser, "SetProcessDPIAware");
				if (pLegacy) pLegacy();
			}
			FreeLibrary(hUser);
		}

		g_pixel_pool_size.store(CalculateOptimalPoolSize(), std::memory_order_relaxed);

#ifdef _WIN64
		g_hCacheFileMutex = CreateMutexW(nullptr, FALSE, L"Global\\ImageSearchDLL_Cache_X64");
		if (!g_hCacheFileMutex) {
			g_hCacheFileMutex = CreateMutexW(nullptr, FALSE, L"ImageSearchDLL_Cache_X64");
		}
#else
		g_hCacheFileMutex = CreateMutexW(nullptr, FALSE, L"Global\\ImageSearchDLL_Cache_x86");
		if (!g_hCacheFileMutex) {
			g_hCacheFileMutex = CreateMutexW(nullptr, FALSE, L"ImageSearchDLL_Cache_x86");
		}
#endif

		EnumerateMonitors();
		break;
	}

	case DLL_PROCESS_DETACH:
	{
		if (lpReserved == nullptr) {
			try {
				if (g_hCacheFileMutex) {
					CloseHandle(g_hCacheFileMutex);
					g_hCacheFileMutex = nullptr;
				}

				if (g_gdiplusToken != 0) {
					g_gdiplusToken = 0;
				}
			}
			catch (...) {
			}
		}
		else {
			g_gdiplusToken = 0;
		}

		break;
	}

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}

#pragma managed(pop)

/*
 * ----------------------------------------------------------------------------
 *  MIT License
 *  Copyright (c) 2025 Dao Van Trong - TRONG.PRO
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 * ----------------------------------------------------------------------------
 */
