// =================================================================================================
//  ImageSearchDLL - Ultra-Fast Image Search
//  Author: Dao Van Trong - TRONG.PRO
//  Architecture: C++03 Compatible (Windows XP+)
//  Licensed under the MIT License. See LICENSE file for details.
// =================================================================================================

#pragma managed(push, off)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501  // Windows XP

// Define min/max to avoid conflicts with Windows macros
inline int min(int a, int b) { return a < b ? a : b; }
inline int max(int a, int b) { return a > b ? a : b; }
inline float min(float a, float b) { return a < b ? a : b; }
inline float max(float a, float b) { return a > b ? a : b; }

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cwctype>
#include <cmath>
#include <cstring>
#include <io.h>
#include <direct.h>

// SSE2 only for x86
#include <emmintrin.h>

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

static int g_pixel_pool_size = 50;

using namespace Gdiplus;

// ============================================================================
// Replacement for C++11+ features
// ============================================================================

// Simple non-null pointer wrapper (replaces unique_ptr)
template<typename T>
class ScopedPtr {
private:
	T* ptr;

	// Non-copyable
	ScopedPtr(const ScopedPtr&);
	ScopedPtr& operator=(const ScopedPtr&);

public:
	explicit ScopedPtr(T* p = NULL) : ptr(p) {}
	~ScopedPtr() { delete ptr; }

	T* get() const { return ptr; }
	T* operator->() const { return ptr; }
	T& operator*() const { return *ptr; }
	T* release() { T* tmp = ptr; ptr = NULL; return tmp; }
	void reset(T* p = NULL) { delete ptr; ptr = p; }
};

// Thread-safe initialization (replaces std::call_once)
class OnceFlag {
private:
	volatile LONG state;
	CRITICAL_SECTION cs;

public:
	OnceFlag() : state(0) {
		InitializeCriticalSection(&cs);
	}

	~OnceFlag() {
		DeleteCriticalSection(&cs);
	}

	template<typename Func>
	void call(Func f) {
		if (state == 2) return;

		EnterCriticalSection(&cs);
		if (state == 0) {
			f();
			state = 2;
		}
		LeaveCriticalSection(&cs);
	}
};

// Thread-local storage replacement
template<typename T>
class ThreadLocal {
private:
	DWORD tls_index;

public:
	ThreadLocal() {
		tls_index = TlsAlloc();
	}

	~ThreadLocal() {
		TlsFree(tls_index);
	}

	T* get() {
		return static_cast<T*>(TlsGetValue(tls_index));
	}

	void set(T* value) {
		TlsSetValue(tls_index, value);
	}
};

// ============================================================================
// Utility Functions
// ============================================================================
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
		int poolSize = static_cast<int>((totalRAM_GB * 5 < 100ULL) ? totalRAM_GB * 5 : 100ULL);
		return (poolSize > 50) ? poolSize : 50;
	}
	return 50;
}

inline int ComputeAlphaThreshold(bool transparent_enabled, int tolerance) {
	if (!transparent_enabled) return 0;
	if (tolerance <= 0) return 255;
	int thresh = 255 - (tolerance * 255 / 255);
	if (thresh < 0) return 0;
	if (thresh > 255) return 255;
	return thresh;
}

#ifdef _MSC_VER
#define SIMD_ALIGNMENT 16
inline void* AlignedAlloc(size_t size, size_t alignment) {
	return _aligned_malloc(size, alignment);
}
inline void AlignedFree(void* ptr) {
	_aligned_free(ptr);
}
#else
#define SIMD_ALIGNMENT 16
inline void* AlignedAlloc(size_t size, size_t alignment) {
	void* ptr = NULL;
	if (posix_memalign(&ptr, alignment, size) != 0) {
		return NULL;
	}
	return ptr;
}
inline void AlignedFree(void* ptr) {
	free(ptr);
}
#endif

template<typename T>
struct AlignedAllocator {
	typedef T value_type;

	AlignedAllocator() {}

	template<typename U>
	AlignedAllocator(const AlignedAllocator<U>&) {}

	T* allocate(size_t n) {
		void* ptr = AlignedAlloc(n * sizeof(T), SIMD_ALIGNMENT);
		if (!ptr) {
			return NULL;
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

private:
	HANDLE m_hMutex;
	bool m_locked;

	// Non-copyable
	ScopedMutex(const ScopedMutex&);
	ScopedMutex& operator=(const ScopedMutex&);
};

// ============================================================================
// GDI+ Initialization
// ============================================================================

ULONG_PTR g_gdiplusToken = 0;
OnceFlag g_gdiplus_init_flag;

void InitializeGdiplusImpl() {
	GdiplusStartupInput gdiplusStartupInput;
	GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
}

void InitializeGdiplus() {
	g_gdiplus_init_flag.call(InitializeGdiplusImpl);
}

// ============================================================================
// CPU Feature Detection
// ============================================================================

bool g_is_sse2_supported = false;
OnceFlag g_feature_detection_flag;

void DetectCpuFeatures() {
	int cpuInfo[4] = { 0 };

#ifdef _MSC_VER
	__cpuid(cpuInfo, 0);
#else
	__asm__ __volatile__(
		"cpuid"
		: "=a"(cpuInfo[0]), "=b"(cpuInfo[1]), "=c"(cpuInfo[2]), "=d"(cpuInfo[3])
		: "a"(0)
		);
#endif

	int maxLeaf = cpuInfo[0];
	if (maxLeaf < 1) {
		g_is_sse2_supported = false;
		return;
	}

	int regs1[4] = { 0 };
#ifdef _MSC_VER
	__cpuid(regs1, 1);
#else
	__asm__ __volatile__(
		"cpuid"
		: "=a"(regs1[0]), "=b"(regs1[1]), "=c"(regs1[2]), "=d"(regs1[3])
		: "a"(1)
		);
#endif

	bool has_sse2 = (regs1[3] & (1 << 26)) != 0;
	g_is_sse2_supported = has_sse2;
}

void DetectFeaturesImpl() {
	DetectCpuFeatures();
}

void DetectFeatures() {
	g_feature_detection_flag.call(DetectFeaturesImpl);
}

// ============================================================================
// Error Handling
// ============================================================================

enum ErrorCode {
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
	case InvalidPath: return L"Invalid path or image format";
	case FailedToLoadImage: return L"Failed to load image from file";
	case FailedToGetScreenDC: return L"Failed to get screen device context or get valid Source pixels";
	case InvalidSearchRegion: return L"Invalid search region specified";
	case InvalidParameters: return L"Invalid parameters provided";
	case InvalidSourceBitmap: return L"Invalid Source (source) bitmap";
	case InvalidTargetBitmap: return L"Invalid Target (target) bitmap";
	case ResultTooLarge: return L"Result String Too Large";
	case InvalidMonitor: return L"Invalid monitor index";
	default: return L"Unknown error";
	}
}

inline std::wstring FormatError(ErrorCode code) {
	std::wstringstream ss;
	ss << L"{" << static_cast<int>(code) << L"}[]<" << GetErrorMessage(code) << L">";
	return ss.str();
}

// ============================================================================
// PixelBuffer and Pool
// ============================================================================

struct PixelBufferPool;
extern PixelBufferPool g_pixel_pool;

struct PixelBuffer {
	std::vector<COLORREF> pixels;
	int width;
	int height;
	bool has_alpha;
	bool owns_memory;

	PixelBuffer() : width(0), height(0), has_alpha(false), owns_memory(true) {}

	bool IsValid() const {
		return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width * height);
	}

	~PixelBuffer();

private:
	// Non-copyable
	PixelBuffer(const PixelBuffer&);
	PixelBuffer& operator=(const PixelBuffer&);
};

struct MatchResult {
	int x, y, w, h;
	float scale;
	std::wstring source_file;

	MatchResult(int _x = 0, int _y = 0, int _w = 0, int _h = 0, float _scale = 1.0f, const std::wstring& _source = L"")
		: x(_x), y(_y), w(_w), h(_h), scale(_scale), source_file(_source) {
	}
};

// ============================================================================
// Cache System
// ============================================================================

struct CacheEntry {
	POINT position;
	int miss_count;
	DWORD last_used_tick;

	CacheEntry() {
		position.x = 0;
		position.y = 0;
		miss_count = 0;
		last_used_tick = GetTickCount();
	}
};

// LRU cache using std::list (C++03 compatible)
std::list<std::pair<std::wstring, CacheEntry> > g_location_cache_lru;
std::map<std::wstring, std::list<std::pair<std::wstring, CacheEntry> >::iterator> g_location_cache_index;
CRITICAL_SECTION g_cache_cs;
std::wstring g_cache_base_dir;
HANDLE g_hCacheFileMutex = NULL;

void InitializeCacheSystem() {
	InitializeCriticalSection(&g_cache_cs);
}

void CleanupCacheSystem() {
	DeleteCriticalSection(&g_cache_cs);
}

std::wstring GetCacheBaseDir() {
	if (!g_cache_base_dir.empty()) return g_cache_base_dir;

	wchar_t temp_path[MAX_PATH];
	DWORD path_len = GetTempPathW(MAX_PATH, temp_path);
	if (path_len > 0 && path_len < MAX_PATH) {
		g_cache_base_dir = temp_path;
		// Ensure directory exists
		CreateDirectoryW(g_cache_base_dir.c_str(), NULL);
		return g_cache_base_dir;
	}
	return L"";
}

std::wstring GetNormalizedPathKey(const std::wstring& path_str) {
	if (path_str.empty()) return L"";

	// Simple normalization - convert to lowercase
	std::wstring lower_path = path_str;
	for (size_t i = 0; i < lower_path.length(); ++i) {
		lower_path[i] = std::towlower(lower_path[i]);
	}

	// Replace forward slashes with backslashes
	for (size_t i = 0; i < lower_path.length(); ++i) {
		if (lower_path[i] == L'/') {
			lower_path[i] = L'\\';
		}
	}

	return lower_path;
}

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

// Simple string hash function (replaces std::hash)
size_t HashString(const std::wstring& str) {
	size_t hash = 0;
	for (size_t i = 0; i < str.length(); ++i) {
		hash = hash * 31 + str[i];
	}
	return hash;
}

std::wstring GetCacheFileForImage(const std::wstring& cache_key) {
	size_t hasher = HashString(cache_key);
	std::wstringstream ss;
	ss << GetCacheBaseDir() << L"\\~CACHE_IMGSEARCH_V2_" << std::hex << std::uppercase << hasher << L".dat";
	return ss.str();
}

void LoadCacheForImage(const std::wstring& cache_key) {
	if (cache_key.empty()) return;
	if (!g_hCacheFileMutex) return;

	ScopedMutex file_lock(g_hCacheFileMutex);
	if (!file_lock.IsLocked()) return;

	try {
		std::wstring cache_file_path = GetCacheFileForImage(cache_key);
		std::wifstream cache_file(cache_file_path.c_str());
		if (cache_file.is_open()) {
			std::wstring line;
			std::getline(cache_file, line);
			size_t pos = line.find(L'|');
			if (pos != std::wstring::npos) {
				std::wstring x_str = line.substr(0, pos);
				std::wstring y_str = line.substr(pos + 1);

				int x = _wtoi(x_str.c_str());
				int y = _wtoi(y_str.c_str());

				if (x >= -10000 && x <= 50000 && y >= -10000 && y <= 50000) {
					CacheEntry entry;
					entry.position.x = x;
					entry.position.y = y;
					entry.miss_count = 0;
					entry.last_used_tick = GetTickCount();

					EnterCriticalSection(&g_cache_cs);

					std::pair<std::wstring, CacheEntry> cache_pair(cache_key, entry);
					g_location_cache_lru.push_front(cache_pair);
					g_location_cache_index[cache_key] = g_location_cache_lru.begin();

					// SAFETY FIX: Clean up index entries before removing from LRU
					while (g_location_cache_lru.size() > MAX_CACHED_LOCATIONS) {
						std::wstring back_key = g_location_cache_lru.back().first;
						g_location_cache_index.erase(back_key);
						g_location_cache_lru.pop_back();
					}

					LeaveCriticalSection(&g_cache_cs);
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

		// Ensure directory exists
		size_t last_slash = cache_file_path.find_last_of(L"\\/");
		if (last_slash != std::wstring::npos) {
			std::wstring dir = cache_file_path.substr(0, last_slash);
			CreateDirectoryW(dir.c_str(), NULL);
		}

		std::wofstream cache_file(cache_file_path.c_str(), std::ios::trunc);
		if (cache_file.is_open()) {
			cache_file << pos.x << L"|" << pos.y;
		}
	}
	catch (...) {}
}

void RemoveFromCache(const std::wstring& cache_key) {
	if (cache_key.empty()) return;

	{
		EnterCriticalSection(&g_cache_cs);

		std::map<std::wstring, std::list<std::pair<std::wstring, CacheEntry> >::iterator>::iterator it = 
			g_location_cache_index.find(cache_key);

		if (it != g_location_cache_index.end()) {
			g_location_cache_lru.erase(it->second);
			g_location_cache_index.erase(it);
		}

		LeaveCriticalSection(&g_cache_cs);
	}

	try {
		std::wstring cache_file_path = GetCacheFileForImage(cache_key);
		DeleteFileW(cache_file_path.c_str());
	}
	catch (...) {}
}

bool GetCachedLocation(const std::wstring& cache_key, CacheEntry& out_entry) {
	EnterCriticalSection(&g_cache_cs);

	std::map<std::wstring, std::list<std::pair<std::wstring, CacheEntry> >::iterator>::iterator it = 
		g_location_cache_index.find(cache_key);

	if (it != g_location_cache_index.end()) {
		out_entry = it->second->second;
		out_entry.last_used_tick = GetTickCount();

		// Move to front (LRU)
		if (it->second != g_location_cache_lru.begin()) {
			std::pair<std::wstring, CacheEntry> item = *it->second;
			g_location_cache_lru.erase(it->second);
			g_location_cache_lru.push_front(item);
			g_location_cache_index[cache_key] = g_location_cache_lru.begin();
		}

		LeaveCriticalSection(&g_cache_cs);
		return true;
	}

	LeaveCriticalSection(&g_cache_cs);
	return false;
}

void UpdateCachedLocation(const std::wstring& cache_key, const CacheEntry& entry) {
	EnterCriticalSection(&g_cache_cs);

	std::map<std::wstring, std::list<std::pair<std::wstring, CacheEntry> >::iterator>::iterator it = 
		g_location_cache_index.find(cache_key);

	if (it != g_location_cache_index.end()) {
		it->second->second = entry;

		// Move to front
		if (it->second != g_location_cache_lru.begin()) {
			std::pair<std::wstring, CacheEntry> item = *it->second;
			g_location_cache_lru.erase(it->second);
			g_location_cache_lru.push_front(item);
			g_location_cache_index[cache_key] = g_location_cache_lru.begin();
		}
	}
	else {
		std::pair<std::wstring, CacheEntry> new_pair(cache_key, entry);
		g_location_cache_lru.push_front(new_pair);

		while (g_location_cache_lru.size() > MAX_CACHED_LOCATIONS) {
			std::wstring back_key = g_location_cache_lru.back().first;
			g_location_cache_index.erase(back_key);
			g_location_cache_lru.pop_back();
		}

		g_location_cache_index[cache_key] = g_location_cache_lru.begin();
	}

	LeaveCriticalSection(&g_cache_cs);
}

// ============================================================================
// Bitmap Cache
// ============================================================================

struct BitmapCacheEntry {
	PixelBuffer* buffer;
	std::wstring key;

	BitmapCacheEntry() : buffer(NULL) {}
	BitmapCacheEntry(PixelBuffer* buf, const std::wstring& k) : buffer(buf), key(k) {}
};

std::list<BitmapCacheEntry> g_bitmap_cache;
std::map<std::wstring, std::list<BitmapCacheEntry>::iterator> g_bitmap_cache_index;
CRITICAL_SECTION g_bitmap_cache_cs;

void InitializeBitmapCache() {
	InitializeCriticalSection(&g_bitmap_cache_cs);
}

void CleanupBitmapCache() {
	EnterCriticalSection(&g_bitmap_cache_cs);

	for (std::list<BitmapCacheEntry>::iterator it = g_bitmap_cache.begin(); 
		it != g_bitmap_cache.end(); ++it) {
			delete it->buffer;
	}
	g_bitmap_cache.clear();
	g_bitmap_cache_index.clear();

	LeaveCriticalSection(&g_bitmap_cache_cs);
	DeleteCriticalSection(&g_bitmap_cache_cs);
}

PixelBuffer* GetCachedBitmap(const std::wstring& key) {
	EnterCriticalSection(&g_bitmap_cache_cs);

	std::map<std::wstring, std::list<BitmapCacheEntry>::iterator>::iterator it = 
		g_bitmap_cache_index.find(key);

	if (it != g_bitmap_cache_index.end()) {
		BitmapCacheEntry entry = *it->second;

		// Move to front (LRU)
		if (it->second != g_bitmap_cache.begin()) {
			g_bitmap_cache.erase(it->second);
			g_bitmap_cache.push_front(entry);
			g_bitmap_cache_index[key] = g_bitmap_cache.begin();
		}

		LeaveCriticalSection(&g_bitmap_cache_cs);
		return entry.buffer;
	}

	LeaveCriticalSection(&g_bitmap_cache_cs);
	return NULL;
}

void CacheBitmap(const std::wstring& key, PixelBuffer* buffer) {
	EnterCriticalSection(&g_bitmap_cache_cs);

	// Remove existing entry if present
	std::map<std::wstring, std::list<BitmapCacheEntry>::iterator>::iterator it = 
		g_bitmap_cache_index.find(key);

	if (it != g_bitmap_cache_index.end()) {
		delete it->second->buffer;
		g_bitmap_cache.erase(it->second);
		g_bitmap_cache_index.erase(it);
	}

	BitmapCacheEntry new_entry(buffer, key);
	g_bitmap_cache.push_front(new_entry);
	g_bitmap_cache_index[key] = g_bitmap_cache.begin();

	// Limit cache size
	while (g_bitmap_cache.size() > MAX_CACHED_BITMAPS) {
		BitmapCacheEntry& back = g_bitmap_cache.back();
		g_bitmap_cache_index.erase(back.key);
		delete back.buffer;
		g_bitmap_cache.pop_back();
	}

	LeaveCriticalSection(&g_bitmap_cache_cs);
}

// ============================================================================
// PixelBuffer Pool
// ============================================================================

struct PixelBufferPool {
	std::vector<std::vector<COLORREF> > buffers;
	CRITICAL_SECTION cs;
	std::map<size_t, std::vector<std::vector<COLORREF> > > size_buckets;

	PixelBufferPool() {
		InitializeCriticalSection(&cs);
	}

	~PixelBufferPool() {
		DeleteCriticalSection(&cs);
	}

	std::vector<COLORREF> Acquire(size_t size) {
		EnterCriticalSection(&cs);

		size_t bucket_size = ((size + 1023) / 1024) * 1024;

		std::map<size_t, std::vector<std::vector<COLORREF> > >::iterator bucket_it = 
			size_buckets.find(bucket_size);

		if (bucket_it != size_buckets.end() && !bucket_it->second.empty()) {
			std::vector<COLORREF> buf = bucket_it->second.back();
			bucket_it->second.pop_back();
			LeaveCriticalSection(&cs);
			buf.resize(size);
			return buf;
		}

		LeaveCriticalSection(&cs);
		return std::vector<COLORREF>(size);
	}

	void Release(std::vector<COLORREF>& buffer) {
		if (buffer.empty()) return;

		EnterCriticalSection(&cs);

		// CRITICAL FIX: Add to size_buckets (not just buffers) to match Acquire() logic
		size_t buffer_capacity = buffer.capacity();
		size_t bucket_size = ((buffer_capacity + 1023) / 1024) * 1024;

		// Count total buffers across all buckets
		size_t total_buffers = buffers.size();
		for (std::map<size_t, std::vector<std::vector<COLORREF> > >::iterator it = size_buckets.begin();
			it != size_buckets.end(); ++it) {
			total_buffers += it->second.size();
		}

		if (total_buffers < static_cast<size_t>(g_pixel_pool_size)) {
			std::vector<COLORREF> temp;
			temp.swap(buffer);
			size_buckets[bucket_size].push_back(temp);
		}

		LeaveCriticalSection(&cs);
	}
};

PixelBufferPool g_pixel_pool;

// PixelBuffer destructor implementation
PixelBuffer::~PixelBuffer() {
	if (owns_memory && !pixels.empty()) {
		g_pixel_pool.Release(pixels);
	}
}

// ============================================================================
// Image Loading and Processing
// ============================================================================

bool DetectAlphaChannel(const PixelBuffer& buffer) {
	size_t sample_size = (buffer.pixels.size() < 1000) ? buffer.pixels.size() : 1000;
	size_t sample_step = (buffer.pixels.size() > sample_size) ? (buffer.pixels.size() / sample_size) : 1;

	for (size_t i = 0; i < buffer.pixels.size(); i += sample_step) {
		BYTE alpha = (buffer.pixels[i] >> 24) & 0xFF;
		if (alpha < 255) {
			return true;
		}
	}
	return false;
}

bool LoadImageFromFile_GDI(const std::wstring& file_path, PixelBuffer& out_buffer) {
	InitializeGdiplus();

	std::wstring cache_key = L"DECODE_" + GetNormalizedPathKey(file_path);
	PixelBuffer* cached = GetCachedBitmap(cache_key);
	if (cached) {
		out_buffer.width = cached->width;
		out_buffer.height = cached->height;
		out_buffer.has_alpha = cached->has_alpha;
		out_buffer.pixels = cached->pixels;
		out_buffer.owns_memory = false;
		return true;
	}

	ScopedPtr<Bitmap> bitmap(new Bitmap(file_path.c_str()));
	if (!bitmap.get()) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return false;
	}

	Status status = bitmap->GetLastStatus();
	if (status != Ok) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return false;
	}

	int width = bitmap->GetWidth();
	int height = bitmap->GetHeight();
	if (width <= 0 || height <= 0 || width > 32000 || height > 32000) {
		return false;
	}

	PixelFormat format = bitmap->GetPixelFormat();
	bool hasAlpha = (format & PixelFormatAlpha) || (format == PixelFormat32bppARGB);

	BitmapData bitmapData;
	Rect rect(0, 0, width, height);

	if (bitmap->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Ok) {
		return false;
	}

	out_buffer.width = width;
	out_buffer.height = height;
	out_buffer.pixels = g_pixel_pool.Acquire(width * height);
	if (out_buffer.pixels.capacity() < static_cast<size_t>(width * height)) {
		bitmap->UnlockBits(&bitmapData);
		return false;
	}
	out_buffer.pixels.resize(width * height);

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
				int ur = static_cast<int>(r * invA + 0.5f);
				int ug = static_cast<int>(g * invA + 0.5f);
				int ub = static_cast<int>(b * invA + 0.5f);
				r = static_cast<BYTE>((ur < 0) ? 0 : ((ur > 255) ? 255 : ur));
				g = static_cast<BYTE>((ug < 0) ? 0 : ((ug > 255) ? 255 : ug));
				b = static_cast<BYTE>((ub < 0) ? 0 : ((ub > 255) ? 255 : ub));
			}
			out_buffer.pixels[y * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}

	bitmap->UnlockBits(&bitmapData);

	out_buffer.has_alpha = hasAlpha && DetectAlphaChannel(out_buffer);

	// Cache the loaded bitmap
	PixelBuffer* cache_buffer = new PixelBuffer();
	cache_buffer->width = out_buffer.width;
	cache_buffer->height = out_buffer.height;
	cache_buffer->has_alpha = out_buffer.has_alpha;
	cache_buffer->pixels = out_buffer.pixels;
	cache_buffer->owns_memory = false;

	out_buffer.owns_memory = false;

	CacheBitmap(cache_key, cache_buffer);

	return true;
}

bool ScaleBitmap_GDI(const PixelBuffer& source, int newW, int newH, PixelBuffer& out_buffer) {
	if (!source.IsValid()) return false;
	if (newW <= 0 || newH <= 0 || newW > 32000 || newH > 32000) return false;

	// Generate cache key
	size_t source_hash = 0;
	size_t sample_step = (source.pixels.size() > 100) ? (source.pixels.size() / 100) : 1;
	for (size_t i = 0; i < source.pixels.size(); i += sample_step) {
		source_hash ^= source.pixels[i] + 0x9e3779b9 + (source_hash << 6) + (source_hash >> 2);
	}

	std::wstringstream cache_key_ss;
	cache_key_ss << L"SCALED_" << std::hex << source_hash << L"_"
		<< std::dec << source.width << L"x" << source.height
		<< L"_to_" << newW << L"x" << newH;
	std::wstring cache_key = cache_key_ss.str();

	PixelBuffer* cached = GetCachedBitmap(cache_key);
	if (cached) {
		out_buffer.width = cached->width;
		out_buffer.height = cached->height;
		out_buffer.has_alpha = cached->has_alpha;
		out_buffer.pixels = cached->pixels;
		out_buffer.owns_memory = false;
		return true;
	}

	InitializeGdiplus();

	Bitmap srcBitmap(source.width, source.height, PixelFormat32bppARGB);
	BitmapData srcData;
	Rect srcRect(0, 0, source.width, source.height);

	if (srcBitmap.LockBits(&srcRect, ImageLockModeWrite, PixelFormat32bppARGB, &srcData) != Ok) {
		return false;
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
				int ur = static_cast<int>(r * invA + 0.5f);
				int ug = static_cast<int>(g * invA + 0.5f);
				int ub = static_cast<int>(b * invA + 0.5f);
				r = static_cast<BYTE>((ur < 0) ? 0 : ((ur > 255) ? 255 : ur));
				g = static_cast<BYTE>((ug < 0) ? 0 : ((ug > 255) ? 255 : ug));
				b = static_cast<BYTE>((ub < 0) ? 0 : ((ub > 255) ? 255 : ub));
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
		return false;
	}

	BitmapData dstData;
	Rect dstRect(0, 0, newW, newH);

	if (dstBitmap.LockBits(&dstRect, ImageLockModeRead, PixelFormat32bppARGB, &dstData) != Ok) {
		return false;
	}

	out_buffer.width = newW;
	out_buffer.height = newH;
	out_buffer.has_alpha = source.has_alpha;
	out_buffer.pixels = g_pixel_pool.Acquire(newW * newH);
	if (out_buffer.pixels.capacity() < static_cast<size_t>(newW * newH)) {
		dstBitmap.UnlockBits(&dstData);
		return false;
	}
	out_buffer.pixels.resize(newW * newH);

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
				int ur = static_cast<int>(r * invA + 0.5f);
				int ug = static_cast<int>(g * invA + 0.5f);
				int ub = static_cast<int>(b * invA + 0.5f);
				r = static_cast<BYTE>((ur < 0) ? 0 : ((ur > 255) ? 255 : ur));
				g = static_cast<BYTE>((ug < 0) ? 0 : ((ug > 255) ? 255 : ug));
				b = static_cast<BYTE>((ub < 0) ? 0 : ((ub > 255) ? 255 : ub));
			}
			out_buffer.pixels[y * newW + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}

	dstBitmap.UnlockBits(&dstData);

	// Cache the result
	PixelBuffer* cache_result = new PixelBuffer();
	cache_result->width = out_buffer.width;
	cache_result->height = out_buffer.height;
	cache_result->has_alpha = out_buffer.has_alpha;
	cache_result->pixels = out_buffer.pixels;
	cache_result->owns_memory = false;

	out_buffer.owns_memory = false;

	CacheBitmap(cache_key, cache_result);

	return true;
}

bool GetBitmapPixels_GDI(HBITMAP hBitmap, PixelBuffer& out_buffer) {
	if (!hBitmap) return false;

	InitializeGdiplus();

	BITMAP bm;
	if (!GetObject(hBitmap, sizeof(BITMAP), &bm)) return false;

	int width = bm.bmWidth;
	int height = bm.bmHeight;
	if (width <= 0 || height <= 0 || width > 32000 || height > 32000) return false;

	ScopedPtr<Bitmap> bitmap(Bitmap::FromHBITMAP(hBitmap, NULL));
	if (!bitmap.get() || bitmap->GetLastStatus() != Ok) {
		return false;
	}

	BitmapData bitmapData;
	Rect rect(0, 0, width, height);

	if (bitmap->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Ok) {
		return false;
	}

	out_buffer.width = width;
	out_buffer.height = height;

	size_t pixel_count = static_cast<size_t>(width) * height;
	if (pixel_count > 100000000) {
		bitmap->UnlockBits(&bitmapData);
		return false;
	}

	out_buffer.pixels = g_pixel_pool.Acquire(pixel_count);
	out_buffer.pixels.resize(pixel_count);

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
				int ur = static_cast<int>(r * invA + 0.5f);
				int ug = static_cast<int>(g * invA + 0.5f);
				int ub = static_cast<int>(b * invA + 0.5f);
				r = static_cast<BYTE>((ur < 0) ? 0 : ((ur > 255) ? 255 : ur));
				g = static_cast<BYTE>((ug < 0) ? 0 : ((ug > 255) ? 255 : ug));
				b = static_cast<BYTE>((ub < 0) ? 0 : ((ub > 255) ? 255 : ub));
			}
			out_buffer.pixels[y * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}

	bitmap->UnlockBits(&bitmapData);

	out_buffer.has_alpha = DetectAlphaChannel(out_buffer);

	return true;
}

// ============================================================================
// Monitor Enumeration
// ============================================================================

struct MonitorInfo {
	RECT bounds;
	int index;
};

std::vector<MonitorInfo> g_monitors;
CRITICAL_SECTION g_monitors_cs;

void InitializeMonitorSystem() {
	InitializeCriticalSection(&g_monitors_cs);
}

void CleanupMonitorSystem() {
	DeleteCriticalSection(&g_monitors_cs);
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	std::vector<MonitorInfo>* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
	MonitorInfo info;
	info.bounds = *lprcMonitor;
	info.index = static_cast<int>(monitors->size());
	monitors->push_back(info);
	return TRUE;
}

void EnumerateMonitors() {
	EnterCriticalSection(&g_monitors_cs);
	g_monitors.clear();
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&g_monitors));
	LeaveCriticalSection(&g_monitors_cs);
}

bool GetMonitorBounds(int screen_index, RECT& bounds) {
	EnterCriticalSection(&g_monitors_cs);

	// SAFETY FIX: Always re-enumerate to avoid stale data
	g_monitors.clear();
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&g_monitors));

	// BOUNDS CHECK: Validate screen_index range
	if (screen_index <= 0 || screen_index > static_cast<int>(g_monitors.size()) || g_monitors.empty()) {
		LeaveCriticalSection(&g_monitors_cs);
		return false;
	}

	bounds = g_monitors[screen_index - 1].bounds;
	LeaveCriticalSection(&g_monitors_cs);
	return true;
}

static void GetScreenBounds(int iScreen, int& left, int& top, int& width, int& height) {
	if (iScreen > 0) {
		RECT monitorBounds;
		if (GetMonitorBounds(iScreen, monitorBounds)) {
			left   = monitorBounds.left;
			top    = monitorBounds.top;
			width  = monitorBounds.right - monitorBounds.left;
			height = monitorBounds.bottom - monitorBounds.top;
			return;
		}
		else {
			left = 0; top = 0;
			width  = GetSystemMetrics(SM_CXSCREEN);
			height = GetSystemMetrics(SM_CYSCREEN);
		}
	}
	else if (iScreen == 0) {
		left = top = 0;
		width = height = 0;
	}
	else { // iScreen < 0
		left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
		top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
		width  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	}
}

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

		// Clamp values
		if (iLeft < screenLeft) iLeft = screenLeft;
		if (iLeft > screenLeft + screenWidth - 1) iLeft = screenLeft + screenWidth - 1;
		if (iTop < screenTop) iTop = screenTop;
		if (iTop > screenTop + screenHeight - 1) iTop = screenTop + screenHeight - 1;
		if (iRight < iLeft + 1) iRight = iLeft + 1;
		if (iRight > screenLeft + screenWidth) iRight = screenLeft + screenWidth;
		if (iBottom < iTop + 1) iBottom = iTop + 1;
		if (iBottom > screenTop + screenHeight) iBottom = screenTop + screenHeight;
	}

	int width = iRight - iLeft;
	int height = iBottom - iTop;

	if (width <= 0 || height <= 0 || width > 32000 || height > 32000) return NULL;

	HDC hdcScreen = GetDC(NULL);
	if (!hdcScreen) return NULL;

	HDC hdcMem = CreateCompatibleDC(hdcScreen);
	if (!hdcMem) {
		ReleaseDC(NULL, hdcScreen);
		return NULL;
	}

	HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
	if (!hBitmap) {
		DeleteDC(hdcMem);
		ReleaseDC(NULL, hdcScreen);
		return NULL;
	}

	HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
	BOOL success = BitBlt(hdcMem, 0, 0, width, height, hdcScreen, iLeft, iTop, SRCCOPY);

	SelectObject(hdcMem, hOldBitmap);
	DeleteDC(hdcMem);
	ReleaseDC(NULL, hdcScreen);

	if (!success) {
		DeleteObject(hBitmap);
		return NULL;
	}

	return hBitmap;
}

bool CaptureScreen_GDI(int iLeft, int iTop, int iRight, int iBottom, int iScreen, PixelBuffer& out_buffer) {
	HBITMAP hBitmap = CaptureScreenInternal(iLeft, iTop, iRight, iBottom, iScreen);
	if (!hBitmap) return false;

	bool result = GetBitmapPixels_GDI(hBitmap, out_buffer);
	DeleteObject(hBitmap);

	return result;
}

// ============================================================================
// Pixel Comparison (SSE2 + Scalar)
// ============================================================================

namespace PixelComparison {
	inline bool IsValidSearchRegion(int start_x, int start_y, int source_width, int source_height,
		int screen_width, int screen_height) {
			return start_x >= 0 && start_y >= 0 &&
				start_x + source_width <= screen_width &&
				start_y + source_height <= screen_height;
	}

	inline bool CheckApproxMatch_Scalar(
		const PixelBuffer& screen, const PixelBuffer& source,
		int start_x, int start_y, bool transparent_enabled, int tolerance) {

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
						BYTE alpha = (source_pixel >> 24) & 0xFF;
						if (alpha < alpha_threshold) continue;
					}

					COLORREF screen_pixel = screen_row[x];

					int r_diff = static_cast<int>(GetRValue(source_pixel)) - static_cast<int>(GetRValue(screen_pixel));
					int g_diff = static_cast<int>(GetGValue(source_pixel)) - static_cast<int>(GetGValue(screen_pixel));
					int b_diff = static_cast<int>(GetBValue(source_pixel)) - static_cast<int>(GetBValue(screen_pixel));

					if (r_diff < 0) r_diff = -r_diff;
					if (g_diff < 0) g_diff = -g_diff;
					if (b_diff < 0) b_diff = -b_diff;

					if (r_diff > tolerance || g_diff > tolerance || b_diff > tolerance) {
						return false;
					}
				}
			}
			return true;
	}

	inline bool CheckApproxMatch_SSE2(
		const PixelBuffer& screen, const PixelBuffer& source,
		int start_x, int start_y, bool transparent_enabled, int tolerance) {

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

				// Scalar fallback for remaining pixels
				for (; x < source.width; ++x) {
					COLORREF source_pixel = source_row[x];

					if (transparent_enabled) {
						BYTE alpha = (source_pixel >> 24) & 0xFF;
						if (alpha < alpha_threshold) continue;
					}

					COLORREF screen_pixel = screen_row[x];

					int r_diff = static_cast<int>(GetRValue(source_pixel)) - static_cast<int>(GetRValue(screen_pixel));
					int g_diff = static_cast<int>(GetGValue(source_pixel)) - static_cast<int>(GetGValue(screen_pixel));
					int b_diff = static_cast<int>(GetBValue(source_pixel)) - static_cast<int>(GetBValue(screen_pixel));

					if (r_diff < 0) r_diff = -r_diff;
					if (g_diff < 0) g_diff = -g_diff;
					if (b_diff < 0) b_diff = -b_diff;

					if (r_diff > tolerance || g_diff > tolerance || b_diff > tolerance) {
						return false;
					}
				}
			}
			return true;
	}
}

// ============================================================================
// Search Function
// ============================================================================

bool CompareMatchResults(const MatchResult& a, const MatchResult& b) {
	if (a.y != b.y) return a.y < b.y;
	return a.x < b.x;
}

std::vector<MatchResult> SearchForBitmap(
	const PixelBuffer& Source, const PixelBuffer& Target,
	int search_left, int search_top, int tolerance, bool transparent_enabled,
	bool find_all, float scale_factor, const std::wstring& source_file,
	std::wstring& backend_used) {

		std::vector<MatchResult> matches;
		if (Target.width > Source.width || Target.height > Source.height) return matches;

		if (g_is_sse2_supported) {
			backend_used = L"SSE2";
		} else {
			backend_used = L"Scalar";
		}

		for (int y = 0; y <= Source.height - Target.height; ++y) {
			for (int x = 0; x <= Source.width - Target.width; ++x) {
				bool found = false;

				if (g_is_sse2_supported) {
					found = PixelComparison::CheckApproxMatch_SSE2(Source, Target, x, y, transparent_enabled, tolerance);
				} else {
					found = PixelComparison::CheckApproxMatch_Scalar(Source, Target, x, y, transparent_enabled, tolerance);
				}

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
// Main Search Functions
// ============================================================================

enum SearchMode {
	ScreenSearch,
	SearchImageInImage,
	HBitmapSearch
};

// =================================================================================================
// SearchParams: Unified parameter structure for all image search operations (Windows XP version)
// =================================================================================================
// Cache System:
// - use_cache = 0: Disables caching completely (default for compatibility)
// - use_cache = 1: Enables in-memory + persistent disk cache for faster repeated searches
//   * Cache is validated on each lookup to ensure accuracy
//   * Invalid cache entries are automatically removed after 3 misses
//   * Cache persists across DLL reloads for better performance
// =================================================================================================
struct SearchParams {
    /**
     * Search mode:
     * - ScreenSearch: Search for images on the screen
     * - SearchImageInImage: Search for images within another image
     * - HBitmapSearch: Search for images within a provided HBITMAP
     */
    SearchMode mode;

    /**
     * List of image files to search for (separated by '|')
     * - Used in ScreenSearch mode
     */
    const wchar_t* image_files;

    /**
     * Search region coordinates (left, top, right, bottom)
     * - Used in ScreenSearch and SearchImageInImage modes
     */
    int left, top, right, bottom;

    /**
     * Screen number to capture (0 = primary screen, 1 = secondary screen, etc.)
     * - Used in ScreenSearch mode
     */
    int screen;

    /**
     * Source image file path
     * - Used in SearchImageInImage mode
     */
    const wchar_t* source_image;

    /**
     * List of target image files to search for (separated by '|')
     * - Used in SearchImageInImage mode
     */
    const wchar_t* target_images;

    /**
     * Source HBITMAP handle
     * - Used in HBitmapSearch mode
     */
    HBITMAP Source_hbitmap;

    /**
     * Target HBITMAP handle
     * - Used in HBitmapSearch mode
     */
    HBITMAP Target_hbitmap;

    /**
     * Color tolerance for image matching (0-255)
     */
    int tolerance;

    /**
     * Maximum number of results to return
     */
    int max_results;

    /**
     * Center position of the search region (1 = center, 0 = top-left)
     */
    int center_pos;

    /**
     * Minimum scale factor for image matching (1.0 = original size)
     */
    float min_scale;

    /**
     * Maximum scale factor for image matching (1.0 = original size)
     */
    float max_scale;

    /**
     * Scale step for image matching (0.1 = 10% increments)
     */
    float scale_step;

    /**
     * Return debug information (0 = disabled, 1 = enabled)
     */
    int return_debug;

    /**
     * Enable caching (0 = disabled, 1 = enabled)
     * - See Cache System documentation above
     */
    int use_cache;  // 0 = disabled, 1 = enabled
    
    /**
     * Default constructor
     */
    SearchParams() 
        : mode(ScreenSearch), image_files(NULL), left(0), top(0), right(0), bottom(0), screen(0),
          source_image(NULL), target_images(NULL), Source_hbitmap(NULL), Target_hbitmap(NULL),
          tolerance(10), max_results(1), center_pos(1), min_scale(1.0f), max_scale(1.0f),
          scale_step(0.1f), return_debug(0), use_cache(0) {}  
};

// Helper to split string by delimiter
void SplitString(const std::wstring& str, wchar_t delimiter, std::vector<std::wstring>& tokens) {
	tokens.clear();
	size_t start_pos = 0;
	while (start_pos < str.length()) {
		size_t end_pos = str.find(delimiter, start_pos);
		if (end_pos == std::wstring::npos) {
			end_pos = str.length();
		}

		if (end_pos > start_pos) {
			tokens.push_back(str.substr(start_pos, end_pos - start_pos));
		}

		if (end_pos == str.length()) break;
		start_pos = end_pos + 1;
	}
}

std::wstring UnifiedImageSearch(const SearchParams& params) {
	DWORD start_tick = GetTickCount();

	DetectFeatures();
	InitializeGdiplus();

	std::wstringstream result_stream;

	int tolerance = (params.tolerance < 0) ? 0 : ((params.tolerance > 255) ? 255 : params.tolerance);
	float min_scale = (params.min_scale < 0.1f) ? 0.1f : ((params.min_scale > 5.0f) ? 5.0f : params.min_scale);
	float max_scale = (params.max_scale < min_scale) ? min_scale : ((params.max_scale > 5.0f) ? 5.0f : params.max_scale);
	float scale_step = (params.scale_step < 0.01f) ? 0.01f : ((params.scale_step > 1.0f) ? 1.0f : params.scale_step);
	scale_step = static_cast<float>(static_cast<int>(scale_step * 10.0f + 0.5f)) / 10.0f;

	PixelBuffer Source;
	std::wstring Source_source;
	int search_offset_x = 0, search_offset_y = 0;

	int capture_left = 0, capture_top = 0, capture_right = 0, capture_bottom = 0;
	int capture_width = 0, capture_height = 0;

	if (params.mode == ScreenSearch) {
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
			left = (params.left < screenLeft) ? screenLeft : ((params.left > screenRight - 1) ? screenRight - 1 : params.left);
			top = (params.top < screenTop) ? screenTop : ((params.top > screenBottom - 1) ? screenBottom - 1 : params.top);
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
                    DWORD duration = GetTickCount() - start_tick;
                    result_stream << FormatError(InvalidSearchRegion);

                    if (params.return_debug > 0) {
                        result_stream << L"(time=" << duration << L"ms"
                            << L", params=left:" << left << L",top:" << top << L",right:" << right << L",bottom:" << bottom
                            << L",screen:" << params.screen
                            << L",use_cache:" << params.use_cache << L")";
                    }
                    return result_stream.str();
                }

                if (!CaptureScreen_GDI(capture_left, capture_top, capture_right, capture_bottom, params.screen, Source)) {
                    DWORD duration = GetTickCount() - start_tick;
                    result_stream << FormatError(FailedToGetScreenDC);
                    if (params.return_debug > 0) {
                        result_stream << L"(time=" << duration << L"ms, use_cache:" << params.use_cache << L")";
                    }
                    return result_stream.str();
                }

                search_offset_x = capture_left;
                search_offset_y = capture_top;
                Source_source = L"Screen";

            }
            else if (params.mode == SearchImageInImage) {
                if (!params.source_image || wcslen(params.source_image) == 0) {
                    DWORD duration = GetTickCount() - start_tick;
                    result_stream << FormatError(InvalidParameters);
                    if (params.return_debug > 0) {
                        result_stream << L"(time=" << duration << L"ms, use_cache:" << params.use_cache << L")";
                    }
                    return result_stream.str();
                }

                if (!LoadImageFromFile_GDI(params.source_image, Source)) {
                    DWORD duration = GetTickCount() - start_tick;
                    result_stream << FormatError(FailedToLoadImage);
                    if (params.return_debug > 0) {
                        result_stream << L"(time=" << duration << L"ms, use_cache:" << params.use_cache << L")";
                    }
                    return result_stream.str();
                }

                Source_source = params.source_image;

            }
            else {
                if (!params.Source_hbitmap) {
                    DWORD duration = GetTickCount() - start_tick;
                    result_stream << FormatError(InvalidSourceBitmap);
                    if (params.return_debug > 0) {
                        result_stream << L"(time=" << duration << L"ms, use_cache:" << params.use_cache << L")";
                    }
                    return result_stream.str();
                }

                if (!GetBitmapPixels_GDI(params.Source_hbitmap, Source)) {
                    DWORD duration = GetTickCount() - start_tick;
                    result_stream << FormatError(InvalidSourceBitmap);
                    if (params.return_debug > 0) {
                        result_stream << L"(time=" << duration << L"ms, use_cache:" << params.use_cache << L")";
                    }
                    return result_stream.str();
                }

                if (params.left != 0 || params.top != 0 || params.right != 0 || params.bottom != 0) {
                    if (Source.IsValid()) {
                        int left = (params.left < 0) ? 0 : ((params.left > Source.width - 1) ? Source.width - 1 : params.left);
                        int top = (params.top < 0) ? 0 : ((params.top > Source.height - 1) ? Source.height - 1 : params.top);
                        int right = (params.right <= left || params.right > Source.width) ? Source.width : params.right;
                        int bottom = (params.bottom <= top || params.bottom > Source.height) ? Source.height : params.bottom;

                        if (left < right && top < bottom) {
                            PixelBuffer cropped;
                            cropped.width = right - left;
                            cropped.height = bottom - top;
                            cropped.has_alpha = Source.has_alpha;
                            cropped.pixels = g_pixel_pool.Acquire(cropped.width * cropped.height);
                            cropped.pixels.resize(cropped.width * cropped.height);

                            for (int y = 0; y < cropped.height; ++y) {
                                const COLORREF* src_row = &Source.pixels[(top + y) * Source.width + left];
                                COLORREF* dst_row = &cropped.pixels[y * cropped.width];
                                memcpy(dst_row, src_row, cropped.width * sizeof(COLORREF));
                            }

                            // Move cropped to Source (manual move semantics)
                            Source.pixels.swap(cropped.pixels);
                            Source.width = cropped.width;
                            Source.height = cropped.height;

                            search_offset_x = left;
                            search_offset_y = top;
                        }
                    }
                }
                Source_source = L"HBITMAP";
            }

            if (!Source.IsValid()) {
                DWORD duration = GetTickCount() - start_tick;
                result_stream << FormatError(FailedToGetScreenDC);
                if (params.return_debug > 0) {
                    result_stream << L"(time=" << duration << L"ms, use_cache:" << params.use_cache << L")";
                }
                return result_stream.str();
            }

            const wchar_t* target_file_list = NULL;
            if (params.mode == ScreenSearch) {
                target_file_list = params.image_files;
            }
            else if (params.mode == SearchImageInImage) {
                target_file_list = params.target_images;
            }

            std::vector<std::wstring> target_files;

            if (params.mode == HBitmapSearch) {
                target_files.push_back(L"HBITMAP");
            }
            else if (target_file_list && wcslen(target_file_list) > 0) {
                SplitString(std::wstring(target_file_list), L'|', target_files);
            }

            if (target_files.empty()) {
                DWORD duration = GetTickCount() - start_tick;
                result_stream << FormatError(InvalidParameters);
                if (params.return_debug > 0) {
                    result_stream << L"(time=" << duration << L"ms, use_cache:" << params.use_cache << L")";
                }
                return result_stream.str();
            }

            std::vector<MatchResult> all_matches;
            bool find_all = (params.max_results >= 2);
            int cache_hits = 0, cache_misses = 0;
            std::wstring backend_used;

	bool skip_scaling = (fabsf(min_scale - 1.0f) < 0.001f && fabsf(max_scale - 1.0f) < 0.001f);

	for (size_t i = 0; i < target_files.size(); ++i) {
		PixelBuffer Target;
		bool Target_loaded = false;

		if (params.mode == HBitmapSearch) {
			if (params.Target_hbitmap) {
				Target_loaded = GetBitmapPixels_GDI(params.Target_hbitmap, Target);
			}
		} else {
			Target_loaded = LoadImageFromFile_GDI(target_files[i], Target);
		}

		if (!Target_loaded || !Target.IsValid()) continue;

		bool transparent_enabled = Target.has_alpha;
		std::wstring source_file = (i < target_files.size()) ? target_files[i] : L"";

		std::vector<MatchResult> current_file_matches;

		if (skip_scaling) {
			std::wstring cache_key;
			if (!source_file.empty() && params.use_cache) {
				cache_key = GenerateCacheKey(Source_source, source_file, tolerance, transparent_enabled, 1.0f);
			}

			bool found_in_cache = false;

			if (!cache_key.empty() && params.use_cache) {
				CacheEntry cached_entry;
				if (GetCachedLocation(cache_key, cached_entry)) {
					// Validate cached position is within current search region
					int cached_abs_x = cached_entry.position.x;
					int cached_abs_y = cached_entry.position.y;

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
										current_file_matches.push_back(MatchResult(cached_entry.position.x, cached_entry.position.y,
											Target.width, Target.height, 1.0f, source_file));
										found_in_cache = true;
										cache_hits++;

										CacheEntry updated = cached_entry;
										updated.miss_count = 0;
										UpdateCachedLocation(cache_key, updated);
									}
									else {
										cache_misses++;
										CacheEntry updated = cached_entry;
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
				std::vector<MatchResult> matches = SearchForBitmap(Source, Target, search_offset_x, search_offset_y,
					tolerance, transparent_enabled, find_all, 1.0f, source_file, backend_used);

				// Always add matches to results, regardless of cache setting
				if (!matches.empty()) {
					current_file_matches.insert(current_file_matches.end(), matches.begin(), matches.end());

					// Save to cache only if caching is enabled
					if (params.use_cache && !cache_key.empty()) {
						POINT new_pos;
						new_pos.x = matches[0].x;
						new_pos.y = matches[0].y;
						CacheEntry entry;
						entry.position = new_pos;
						entry.miss_count = 0;
						entry.last_used_tick = GetTickCount();
						UpdateCachedLocation(cache_key, entry);
						SaveCacheForImage(cache_key, new_pos);
					}
				}
			}
		}
		else {
			std::vector<float> scales;
			for (float scale = min_scale; scale <= max_scale; scale += scale_step) {
				scales.push_back(static_cast<float>(static_cast<int>(scale * 10.0f + 0.5f)) / 10.0f);
			}

			for (size_t s = 0; s < scales.size(); ++s) {
				float scale = scales[s];

				int newW = static_cast<int>(Target.width * scale + 0.5f);
				int newH = static_cast<int>(Target.height * scale + 0.5f);
				if (newW <= 0 || newH <= 0 || newW > Source.width || newH > Source.height) {
					continue;
				}

				PixelBuffer scaled;
				if (ScaleBitmap_GDI(Target, newW, newH, scaled) && scaled.IsValid()) {
					std::vector<MatchResult> matches = SearchForBitmap(Source, scaled, search_offset_x, search_offset_y,
						tolerance, transparent_enabled, find_all, scale, source_file, backend_used);
					if (!matches.empty()) {
						current_file_matches.insert(current_file_matches.end(), matches.begin(), matches.end());
						if (!find_all) break;
					}
				}
			}
		}

		if (!current_file_matches.empty()) {
			all_matches.insert(all_matches.end(), current_file_matches.begin(), current_file_matches.end());
			if (!find_all) break;
		}
	}

	DWORD duration = GetTickCount() - start_tick;

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

	if (params.return_debug > 0) {
		result_stream << L"(time=" << duration << L"ms"
			<< L", backend=" << backend_used
			<< L", Source=" << Source.width << L"x" << Source.height
			<< L", files=" << target_files.size()
			<< L", cache_hits=" << cache_hits
			<< L", cache_misses=" << cache_misses
			<< L", use_cache=" << params.use_cache
			<< L", tolerance=" << tolerance
			<< L", scale=" << FormatFloat(min_scale) << L"-" << FormatFloat(max_scale) << L":" << FormatFloat(scale_step)
			<< L", cpu=SSE2:" << (g_is_sse2_supported ? L"Y" : L"N")
			<< L", capture=" << capture_left << L"|" << capture_top << L"|" << capture_right << L"|" << capture_bottom
			<< L"|" << capture_width << L"x" << capture_height
			<< L", screen=" << params.screen
			<< L")";
	}

	return result_stream.str();
}

static void CheckResultBufferSize(std::wstring& result_buffer, const SearchParams& params) {
	if (result_buffer.length() >= MAX_RESULT_STRING_LENGTH) {
		result_buffer = FormatError(ResultTooLarge);

		if (params.return_debug > 0) {
			std::wstringstream ss;
			ss << L"(buffer_size=" << result_buffer.length() << L")";
			result_buffer += ss.str();
		}
	}
}
// Helper function to convert screen coordinates to absolute coordinates
static void ScreenToAbsolute(int x, int y, LONG& ax, LONG& ay) {
    LONG64 screenWidth  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    LONG64 screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (screenWidth <= 0)  screenWidth  = 1;
    if (screenHeight <= 0) screenHeight = 1;

    // Calculate with LONG64 first to prevent overflow, then cast
    LONG64 ax_temp = (static_cast<LONG64>(x) * 65535LL) / screenWidth;
    LONG64 ay_temp = (static_cast<LONG64>(y) * 65535LL) / screenHeight;

    // Manual clamp for C++03 compatibility
    if (ax_temp < 0) ax_temp = 0;
    if (ax_temp > 65535) ax_temp = 65535;
    if (ay_temp < 0) ay_temp = 0;
    if (ay_temp > 65535) ay_temp = 65535;

    ax = static_cast<LONG>(ax_temp);
    ay = static_cast<LONG>(ay_temp);
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
	} else if (btn == L"right" || btn == L"menu" || btn == L"secondary") {
		downFlag = MOUSEEVENTF_RIGHTDOWN;
		upFlag = MOUSEEVENTF_RIGHTUP;
	} else if (btn == L"middle") {
		downFlag = MOUSEEVENTF_MIDDLEDOWN;
		upFlag = MOUSEEVENTF_MIDDLEUP;
	} else {
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
	} else {
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
	} else {
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
		} else if (matchPos == windowTitle) {
			candidate.titleMatchQuality = 1; // Starts with
		} else {
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
	} else if (iswdigit(title[0])) {
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

extern "C" __declspec(dllexport) int WINAPI ImageSearch_MouseClick(
	const wchar_t* sButton,
	int iX,
	int iY,
	int iClicks,
	int iSpeed,
	int iScreen
	) {
		if (!sButton) return 0;

		// Clamp parameters
		if (iClicks < 1) iClicks = 1;  // Fixed typo: iClamps → iClicks
		if (iSpeed < 0) iSpeed = 0;
		if (iSpeed > 100) iSpeed = 100;

		// Get current cursor position
		POINT currentPos;
		GetCursorPos(&currentPos);

		bool needMove = (iX != -1 && iY != -1);
		bool restorePosition = (iSpeed == 0 && needMove);

		int targetX = currentPos.x;
		int targetY = currentPos.y;

		// If coordinates specified, calculate target position
		if (needMove) {
			// Apply screen offset based on iScreen
			if (iScreen > 0) {
				// Specific monitor (1-based): Coordinates are relative to monitor
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
			// iScreen == 0: No offset (primary monitor coordinates)

			// Perform click with SendInput (auto-fallback to mouse_event if fail)
			PerformClick(targetX, targetY, sButton, iClicks, iSpeed, restorePosition);
		} else {
			// Click at current position
			PerformClick(currentPos.x, currentPos.y, sButton, iClicks, iSpeed, false);
		}

		return 1;
}

// ============================================================================
// EXPORTED FUNCTION: ImageSearch_MouseMove
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
// Exported Functions
// ============================================================================

ThreadLocal<std::wstring> g_tls_result_buffer;

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
		std::wstring* result_buffer = g_tls_result_buffer.get();
		if (!result_buffer) {
			result_buffer = new std::wstring();
			g_tls_result_buffer.set(result_buffer);
		}

		SearchParams params;
		params.mode = ScreenSearch;
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

		*result_buffer = UnifiedImageSearch(params);
		CheckResultBufferSize(*result_buffer, params);

		return result_buffer->c_str();
}

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
		std::wstring* result_buffer = g_tls_result_buffer.get();
		if (!result_buffer) {
			result_buffer = new std::wstring();
			g_tls_result_buffer.set(result_buffer);
		}

		SearchParams params;
		params.mode = SearchImageInImage;
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

		*result_buffer = UnifiedImageSearch(params);
		CheckResultBufferSize(*result_buffer, params);

		return result_buffer->c_str();
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
		std::wstring* result_buffer = g_tls_result_buffer.get();
		if (!result_buffer) {
			result_buffer = new std::wstring();
			g_tls_result_buffer.set(result_buffer);
		}

		SearchParams params;
		params.mode = HBitmapSearch;
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

		*result_buffer = UnifiedImageSearch(params);
		CheckResultBufferSize(*result_buffer, params);

		return result_buffer->c_str();
}

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

extern "C" __declspec(dllexport) HBITMAP WINAPI ImageSearch_hBitmapLoad(
	const wchar_t* sImageFile, 
	int iAlpha = 0,
	int iRed = 0,
	int iGreen = 0,
	int iBlue = 0
	) {
		if (!sImageFile || wcslen(sImageFile) == 0) {
			return NULL;
		}

		// Check if file exists
		DWORD fileAttr = GetFileAttributesW(sImageFile);
		if (fileAttr == INVALID_FILE_ATTRIBUTES || (fileAttr & FILE_ATTRIBUTE_DIRECTORY)) {
			return NULL;
		}

		InitializeGdiplus();

		// Load bitmap from file
		ScopedPtr<Bitmap> bitmap(new Bitmap(sImageFile));
		if (!bitmap.get() || bitmap->GetLastStatus() != Ok) {
			return NULL;
		}

		int width = bitmap->GetWidth();
		int height = bitmap->GetHeight();
		if (width <= 0 || height <= 0 || width > 32000 || height > 32000) {
			return NULL;
		}

		// Clamp color values to valid range (0-255)
		int alpha = (iAlpha < 0) ? 0 : ((iAlpha > 255) ? 255 : iAlpha);
		int red = (iRed < 0) ? 0 : ((iRed > 255) ? 255 : iRed);
		int green = (iGreen < 0) ? 0 : ((iGreen > 255) ? 255 : iGreen);
		int blue = (iBlue < 0) ? 0 : ((iBlue > 255) ? 255 : iBlue);

		// Create background color from ARGB
		Color background(alpha, red, green, blue);

		// Convert to HBITMAP
		HBITMAP hBitmap = NULL;
		if (bitmap->GetHBITMAP(background, &hBitmap) != Ok) {
			return NULL;
		}

		return hBitmap;
}

extern "C" __declspec(dllexport) void WINAPI ImageSearch_ClearCache() {
	{
		EnterCriticalSection(&g_cache_cs);
		g_location_cache_lru.clear();
		g_location_cache_index.clear();
		LeaveCriticalSection(&g_cache_cs);
	}

	try {
		std::wstring cache_dir = GetCacheBaseDir();
		if (!cache_dir.empty()) {
			WIN32_FIND_DATAW findData;
			std::wstring search_pattern = cache_dir + L"\\~CACHE_IMGSEARCH_*.dat";
			HANDLE hFind = FindFirstFileW(search_pattern.c_str(), &findData);

			if (hFind != INVALID_HANDLE_VALUE) {
				do {
					std::wstring file_path = cache_dir + L"\\" + findData.cFileName;
					DeleteFileW(file_path.c_str());
				} while (FindNextFileW(hFind, &findData));
				FindClose(hFind);
			}
		}
	}
	catch (...) {}

	{
		EnterCriticalSection(&g_bitmap_cache_cs);

		for (std::list<BitmapCacheEntry>::iterator it = g_bitmap_cache.begin();
			it != g_bitmap_cache.end(); ++it) {
				delete it->buffer;
		}

		g_bitmap_cache.clear();
		g_bitmap_cache_index.clear();
		LeaveCriticalSection(&g_bitmap_cache_cs);
	}
}

extern "C" __declspec(dllexport) const wchar_t* WINAPI ImageSearch_GetVersion() {
#ifdef _WIN64
	return L"ImageSearchDLL v3.3 [x64 XP] 2025.10.15  ::  Dao Van Trong - TRONG.PRO";
#else
	return L"ImageSearchDLL v3.3 [x86 XP] 2025.10.15  ::  Dao Van Trong - TRONG.PRO";
#endif
}

extern "C" __declspec(dllexport) const wchar_t* WINAPI ImageSearch_GetSysInfo() {
	static wchar_t info_buffer[1024];

	DetectFeatures();
	EnumerateMonitors();

	swprintf_s(info_buffer, sizeof(info_buffer) / sizeof(wchar_t),
		L"CPU: SSE2=%s | Screen: %dx%d | Monitors=%d | LocationCache: %d/%d | BitmapCache: %d/%d | PoolSize: %d",
		g_is_sse2_supported ? L"Yes" : L"No",
		GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
		static_cast<int>(g_monitors.size()),
		static_cast<int>(g_location_cache_lru.size()), MAX_CACHED_LOCATIONS,
		static_cast<int>(g_bitmap_cache.size()), MAX_CACHED_BITMAPS,
		g_pixel_pool_size);

	return info_buffer;
}

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		{
			typedef BOOL(WINAPI* PFN_SetProcessDPIAware)();

			HMODULE hUser = LoadLibraryW(L"user32.dll");
			if (hUser) {
				PFN_SetProcessDPIAware pLegacy = (PFN_SetProcessDPIAware)GetProcAddress(hUser, "SetProcessDPIAware");
				if (pLegacy) pLegacy();
				FreeLibrary(hUser);
			}

			g_pixel_pool_size = CalculateOptimalPoolSize();


#ifdef _WIN64
			g_hCacheFileMutex = CreateMutexW(NULL, FALSE, L"Global\\ImageSearchDLL_Cache_x64");
			if (!g_hCacheFileMutex) {
				g_hCacheFileMutex = CreateMutexW(NULL, FALSE, L"ImageSearchDLL_Cache_x64");
			}
#else
			g_hCacheFileMutex = CreateMutexW(NULL, FALSE, L"Global\\ImageSearchDLL_Cache_x86");
			if (!g_hCacheFileMutex) {
				g_hCacheFileMutex = CreateMutexW(NULL, FALSE, L"ImageSearchDLL_Cache_x86");
			}
#endif
			InitializeCacheSystem();
			InitializeBitmapCache();
			InitializeMonitorSystem();
			EnumerateMonitors();
			break;
		}

	case DLL_PROCESS_DETACH:
		{
			if (lpReserved == NULL) {
				try {
					if (g_hCacheFileMutex) {
						CloseHandle(g_hCacheFileMutex);
						g_hCacheFileMutex = NULL;
					}

					if (g_gdiplusToken != 0) {
						g_gdiplusToken = 0;
					}

					CleanupCacheSystem();
					CleanupBitmapCache();
					CleanupMonitorSystem();
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
