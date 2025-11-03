# ImageSearchDLL - Ultra-Fast Image Search

## Overview

ImageSearchDLL is a high-performance dynamic link library (DLL) designed for ultra-fast image searching on the screen, within other images, or using bitmap handles. It leverages SIMD instructions (SSE2, AVX2, AVX512) for optimized performance and includes features like caching, scaling, tolerance matching, and integrated mouse click functionality. The DLL is compatible with Windows environments and supports both 32-bit (x86) and 64-bit (x64) architectures.

- **Author**: Dao Van Trong - TRONG.PRO
- **DLL Version**: v3.3 
- **License:** MIT

Two variants are provided:
- **ImageSearchDLL_XP.cpp**: C++03 compatible, supports Windows XP+ (legacy systems).
- **ImageSearchDLL.cpp**: Using modern c++, only supported from Win 7 SP1 (AVX2/AVX512 support on x64).

## ☕ Support My Work

Enjoy my work? [Buy me a 🍻](https://buymeacoffee.com/trong) or tip via ❤️ [PayPal](https://paypal.me/DaoVanTrong)

Your support helps me continue developing and maintaining this library for the community! 🙏

## Features

- **Ultra-Fast Search**: Uses SIMD-accelerated algorithms for rapid pixel matching.
- **Search Modes**:
  - Screen search (capture and search on desktop or specific monitors).
  - Image-in-image search.
  - HBITMAP-based search.
- **Scaling Support**: Search with variable scales (min/max scale, step size).
- **Tolerance Matching**: Adjustable color tolerance for fuzzy matches.
- **Transparency Handling**: Optional alpha channel support.
- **Caching System**: 
  - In-memory LRU cache for locations and bitmaps
  - Persistent disk cache (survives DLL reload)
  - Optional cache control via `iUseCache` parameter
  - Automatic cache validation and cleanup
- **Multi-Monitor Support**: Handles virtual screens and specific monitors.
- **Mouse Click Integration**: Click at found positions or windows with customizable speed and buttons.
- **Debug Output**: Optional detailed debug information in results.
- **System Info**: Retrieve CPU features, screen details, and cache status.
- **Pool Management**: Efficient memory pooling for pixel buffers.

## Requirements

- **Operating System**:
  - XP Version: Windows XP or later.
  - Modern Version: Windows 7 or later (recommended for AVX support).
- **Compiler**: Microsoft Visual C++ (MSVC) or compatible.
- **Dependencies**: GDI+ (included in Windows), no external libraries needed.
- **CPU**: SSE2 required for basic functionality; AVX2/AVX512 for enhanced performance on x64.

## For AutoIt Users

**⚠️ AutoIt users should use the UDF wrapper instead of calling DLL directly.**

The ImageSearchDLL_UDF.au3 wrapper provides:
- ✅ Easy-to-use functions with AutoIt-friendly syntax
- ✅ Automatic error handling and validation
- ✅ Multi-monitor support with WinAPI fallback (v3.3)
- ✅ Built-in monitor enumeration and coordinate conversion
- ✅ Reliable mouse operations on all configurations

👉 **See README_UDF.md for AutoIt documentation and examples.**

## Installation (C++/Other Languages)

1. **Build the DLL**:
   - Open the `.cpp` file in Visual Studio.
   - Set platform to x86 or x64.
   - Build as a DLL project.
   - Output: `ImageSearchDLL_x64.dll` or `ImageSearchDLL_x86.dll`

2. **Usage in Code**:
   - Load the DLL dynamically using `LoadLibraryW`.
   - Get function pointers with `GetProcAddress`.
   - See C++ examples below.

## API Reference

The DLL exports several functions. All string parameters are wide strings (`wchar_t*`). Results are returned as `const wchar_t*` strings in formats like `{error_code}[results]<error_message>` or `1|x|y|w|h|scale|source` for successful matches.

### Core Search Functions

- **`const wchar_t* WINAPI ImageSearch(const wchar_t* sImageFile, int iLeft=0, int iTop=0, int iRight=0, int iBottom=0, int iScreen=0, int iTolerance=10, int iResults=1, int iCenterPOS=1, float fMinScale=1.0f, float fMaxScale=1.0f, float fScaleStep=0.1f, int iReturnDebug=0, int iUseCache=0)`**
  - Searches for image(s) on the screen.
  - `sImageFile`: Path to target image(s), supports wildcards (e.g., `*img*.png`).
  - Region: `iLeft`, `iTop`, `iRight`, `iBottom` (0 for full screen).
  - `iScreen`: Monitor index (1-based; 0 for primary, negative for virtual).
  - `iUseCache`: 0=disabled (default), 1=enabled (use persistent cache).
  - Returns: Number of matches followed by details, or error.

- **`const wchar_t* WINAPI ImageSearch_InImage(const wchar_t* sSourceImageFile, const wchar_t* sTargetImageFile, int iTolerance=10, int iResults=1, int iCenterPOS=1, float fMinScale=1.0f, float fMaxScale=1.0f, float fScaleStep=0.1f, int iReturnDebug=0, int iUseCache=0)`**
  - Searches for target image within a source image file.
  - `iUseCache`: 0=disabled (default), 1=enabled (use persistent cache).

- **`const wchar_t* WINAPI ImageSearch_hBitmap(HBITMAP hBitmapSource, HBITMAP hBitmapTarget, int iTolerance, int iLeft, int iTop, int iRight, int iBottom, int iResults=1, int iCenter=1, float fMinScale=1.0f, float fMaxScale=1.0f, float fScaleStep=0.1f, int iReturnDebug=0, int iUseCache=0)`**
  - Searches using bitmap handles.
  - `iUseCache`: 0=disabled (default), 1=enabled (use persistent cache).

### Utility Functions

- **`HBITMAP WINAPI ImageSearch_CaptureScreen(int iLeft=0, int iTop=0, int iRight=0, int iBottom=0, int iScreen=0)`**
  - Captures a screen region as HBITMAP.

- **`HBITMAP WINAPI ImageSearch_hBitmapLoad(const wchar_t* sImageFile, int iAlpha=0, int iRed=0, int iGreen=0, int iBlue=0)`**
  - Loads image as HBITMAP with optional background color.

- **`void WINAPI ImageSearch_ClearCache()`**
  - Clears location and bitmap caches.

- **`const wchar_t* WINAPI ImageSearch_GetVersion()`**
  - Returns DLL version string.

- **`const wchar_t* WINAPI ImageSearch_GetSysInfo()`**
  - Returns system info (CPU features, screen size, cache stats).

### Mouse Click Functions

- **`int WINAPI ImageSearch_MouseMove(int iX, int iY, int iSpeed, int iScreen)`**
  - Moves mouse cursor to screen coordinates.
  - Returns: 1 on success, 0 on failure.
  - **Note**: UDF v3.3 uses WinAPI fallback for reliable multi-monitor support.

- **`int WINAPI ImageSearch_MouseClick(const wchar_t* sButton, int iX, int iY, int iClicks, int iSpeed, int iScreen)`**
  - Clicks at screen coordinates.
  - `sButton`: "left", "right", "middle" (case-insensitive).
  - `iX`, `iY`: Virtual desktop coordinates (supports negative values for multi-monitor).
  - `iScreen`: Monitor index (-1=all, 1=first, 2=second, etc.).
  - Returns: 1 on success, 0 on failure.
  - **Note**: UDF v3.3 uses WinAPI fallback for reliable multi-monitor support.

- **`int WINAPI ImageSearch_MouseClickWin(const wchar_t* sTitle, const wchar_t* sText, int iX, int iY, const wchar_t* sButton, int iClicks, int iSpeed)`**
  - Clicks relative to a window (found by title/text).
  - Returns: 1 on success, 0 on failure.

## Usage Examples

### C++ Example: Basic Screen Search

```cpp
#include <windows.h>
#include <iostream>

typedef const wchar_t* (WINAPI* PFN_ImageSearch)(
    const wchar_t*, int, int, int, int, int, int, int, int, 
    float, float, float, int, int);

int main() {
    // Load DLL
    HMODULE hDll = LoadLibraryW(L"ImageSearchDLL_x64.dll");
    if (!hDll) {
        std::wcerr << L"Failed to load DLL" << std::endl;
        return 1;
    }

    // Get function pointer
    auto pImageSearch = (PFN_ImageSearch)GetProcAddress(hDll, "ImageSearch");
    if (!pImageSearch) {
        std::wcerr << L"Failed to get ImageSearch function" << std::endl;
        FreeLibrary(hDll);
        return 1;
    }

    // Call ImageSearch
    const wchar_t* result = pImageSearch(
        L"target.png",  // Image path
        0, 0,           // Left, Top (0 = full screen)
        1920, 1080,     // Right, Bottom
        -1,             // Screen (-1 = all monitors)
        10,             // Tolerance
        1,              // Max results
        1,              // Center position
        1.0f, 1.0f,     // Min/Max scale
        0.1f,           // Scale step
        1,              // Debug output
        0               // Cache disabled
    );

    std::wcout << L"Result: " << result << std::endl;

    // Parse result: {count}[x|y|w|h,...](debug info)
    // Example: {1}[640|480|32|32](time=50ms...)

    FreeLibrary(hDll);
    return 0;
}
```

### C++ Example: Image-in-Image Search

```cpp
#include <windows.h>
#include <iostream>

typedef const wchar_t* (WINAPI* PFN_ImageSearch_InImage)(
    const wchar_t*, const wchar_t*, int, int, int, 
    float, float, float, int, int);

int main() {
    HMODULE hDll = LoadLibraryW(L"ImageSearchDLL_x64.dll");
    if (!hDll) return 1;

    auto pFunc = (PFN_ImageSearch_InImage)GetProcAddress(hDll, "ImageSearch_InImage");
    if (!pFunc) {
        FreeLibrary(hDll);
        return 1;
    }

    const wchar_t* result = pFunc(
        L"screenshot.png",  // Source image
        L"button.png",      // Target image to find
        15,                 // Tolerance
        5,                  // Max results
        1,                  // Center position
        1.0f, 1.0f, 0.1f,  // Scale parameters
        1,                  // Debug
        0                   // Cache
    );

    std::wcout << L"Result: " << result << std::endl;

    FreeLibrary(hDll);
    return 0;
}
```

### C++ Example: HBITMAP Search

```cpp
#include <windows.h>
#include <iostream>
#include <gdiplus.h>

typedef const wchar_t* (WINAPI* PFN_ImageSearch_hBitmap)(
    HBITMAP, HBITMAP, int, int, int, int, int, int, int, 
    float, float, float, int, int);

int main() {
    HMODULE hDll = LoadLibraryW(L"ImageSearchDLL_x64.dll");
    if (!hDll) return 1;

    auto pFunc = (PFN_ImageSearch_hBitmap)GetProcAddress(hDll, "ImageSearch_hBitmap");
    if (!pFunc) {
        FreeLibrary(hDll);
        return 1;
    }

    // Load bitmaps (pseudo-code, use GDI+ or other methods)
    HBITMAP hSource = LoadBitmapFromFile(L"screenshot.bmp");
    HBITMAP hTarget = LoadBitmapFromFile(L"icon.bmp");

    const wchar_t* result = pFunc(
        hSource, hTarget,   // Source and target bitmaps
        10,                 // Tolerance
        0, 0, 0, 0,        // Search region (0 = entire source)
        1,                  // Max results
        1,                  // Center position
        1.0f, 1.0f, 0.1f,  // Scale
        1,                  // Debug
        0                   // Cache
    );

    std::wcout << L"Result: " << result << std::endl;

    DeleteObject(hSource);
    DeleteObject(hTarget);
    FreeLibrary(hDll);
    return 0;
}
```

### Result Format

Results start with `{code}` for errors (e.g., `{-1}[]<Invalid path or image format>`). Positive numbers indicate match count.


## Performance Tips

### Search Performance
- **Enable Caching**: Set `iUseCache=1` for repeated searches (30-50% faster)
- **Cache Benefits**:
  - In-memory cache: Instant lookup for recent searches
  - Disk cache: Persists across DLL reloads
  - Auto-validation: Removes stale entries after 3 misses
- **Adjust Tolerance**: Higher tolerance = faster but less accurate
- **Scale Steps**: Larger steps = faster but may miss matches
- **SIMD Support**: On x64, ensure AVX2/AVX512 for best performance
- **Memory Pool**: Automatic optimization, no manual tuning needed

### Multi-Monitor Setup
- **Specific Monitor Search**: Use `iScreen=1` or `iScreen=2` for 2-3x faster search
- **Virtual Desktop**: Use `iScreen=-1` to search across all monitors
- **Coordinate System**: DLL returns virtual desktop coordinates (may be negative)
- **Mouse Operations**: Use UDF v3.3 for reliable mouse movement/clicking on all monitors

## Limitations

### DLL Limitations
- Max results: 1024
- Image size limit: 32000x32000 pixels
- Max pixel count: 100M pixels per image
- Cache limits: 100 locations, 100 bitmaps (LRU eviction)
- Thread-safe but single DLL instance recommended per process

### Known Issues (DLL v3.3)
- **Mouse Functions**: DLL mouse functions may not work reliably on multi-monitor setups
  - **Solution**: Use UDF v3.3 which provides WinAPI-based fallback
  - **Impact**: Mouse operations now 100% reliable on all monitor configurations
- **Negative Coordinates**: DLL returns negative coords for monitors positioned left/above primary
  - **Solution**: UDF v3.3 handles this correctly with `SetCursorPos`

## Related Documentation

- **README_UDF.md** - AutoIt wrapper documentation with examples
- **ImageSearchDLL_UDF.au3** - AutoIt wrapper source code
- **ImageSearch TEST Suite.au3** - Interactive GUI test application

## Contributing & Support

For issues, contributions, or commercial licensing:
- **Website**: TRONG.PRO
- **Email**: trong@email.com


## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

**For AutoIt users:** See README_UDF.md for easy-to-use wrapper functions and AutoIt examples.

Thank you for using ImageSearchDLL! 🚀