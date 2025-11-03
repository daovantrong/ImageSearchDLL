# ImageSearchDLL - Ultra-Fast Image Search Library

[![Version](https://img.shields.io/badge/version-3.3-blue.svg)](https://github.com/yourusername/ImageSearchDLL)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)](https://www.microsoft.com/windows)

**ImageSearchDLL** is a high-performance image recognition library designed for Windows automation. Built with C++17 and optimized with SIMD instructions (AVX512/AVX2/SSE2), it provides lightning-fast image searching capabilities for screen automation, computer vision, and testing applications.

**Author**: Dao Van Trong - TRONG.PRO
**DLL Version**: v3.3 
**License:** MIT

---

## 🚀 Key Features

- **⚡ Ultra-Fast Performance** - SIMD optimizations (AVX512/AVX2/SSE2) with automatic CPU detection
- **🎯 Multi-Scale Search** - Find images at different sizes with configurable scale ranges
- **🔍 Flexible Tolerance** - Adjustable color matching tolerance (0-255)
- **🖼️ Transparent Images** - Full alpha channel support for PNG images
- **🔄 Smart Caching** - Two-tier caching system (memory + disk) for repeated searches
- **🖥️ Multi-Monitor Support** - Native support for multi-monitor setups
- **🧵 Multi-Threading** - Automatic parallel processing for large images
- **📍 Multiple Results** - Find single or all occurrences of target images
- **🎮 Mouse Automation** - Built-in mouse click and movement functions
- **🔧 Image-in-Image Search** - Search within image files without screen capture

---

## 📦 DLL Files

The library includes multiple build variants:

| File | Architecture | Runtime | OS Support |
|------|-------------|---------|------------|
| `ImageSearchDLL_x64.dll` | x64 | Dynamic | Windows 10+ |
| `ImageSearchDLL_x86.dll` | x86 | Dynamic | Windows 10+ |
| `ImageSearchDLL_MD_x64.dll` | x64 | MD | Windows 10+ |
| `ImageSearchDLL_MD_x86.dll` | x86 | MD | Windows 10+ |
| `ImageSearchDLL_XP_x64.dll` | x64 | Static | Windows 7+ |
| `ImageSearchDLL_XP_x86.dll` | x86 | Static | Windows 7+ |

**Recommended:** Use `ImageSearchDLL_x64.dll` or `ImageSearchDLL_x86.dll` for modern Windows systems.

---

## 🔌 API Reference

### Core Search Functions

#### ImageSearch
Search for image(s) on the screen with multi-scale support.

**C++ Signature:**
```cpp
const wchar_t* WINAPI ImageSearch(
    const wchar_t* sImageFile,    // Pipe-separated image paths: "img1.png|img2.png"
    int iLeft = 0,                // Search region left coordinate
    int iTop = 0,                 // Search region top coordinate
    int iRight = 0,               // Search region right coordinate (0 = screen width)
    int iBottom = 0,              // Search region bottom coordinate (0 = screen height)
    int iScreen = 0,              // Monitor index (0 = primary, -1 = virtual desktop)
    int iTolerance = 10,          // Color tolerance 0-255 (10 recommended)
    int iResults = 1,             // Max results (1 = first match, >=2 = all matches)
    int iCenterPOS = 1,           // 1 = return center coordinates, 0 = top-left
    float fMinScale = 1.0f,       // Minimum scale factor (0.1-5.0)
    float fMaxScale = 1.0f,       // Maximum scale factor (0.1-5.0)
    float fScaleStep = 0.1f,      // Scale increment (0.01-1.0)
    int iReturnDebug = 0,         // Return debug information (0 = off, 1 = on)
    int iUseCache = 0             // Enable caching (0 = off, 1 = on)
);
```

**Return Format:**
- Success: `{count}[x|y|w|h|scale|source, ...]<debug_info>`
- Error: `{error_code}[]<error_message>`

**Examples:**
```
{1}[100|200|50|30]                          // Single match at (100,200)
{3}[100|200|50|30, 300|400|50|30, ...]     // Multiple matches
{-1}[]<Invalid path or image format>        // Error
```

---

#### ImageSearch_InImage
Search for image(s) within another image file (no screen capture).

**C++ Signature:**
```cpp
const wchar_t* WINAPI ImageSearch_InImage(
    const wchar_t* sSourceImageFile,  // Source image file path
    const wchar_t* sTargetImageFile,  // Pipe-separated target image paths
    int iTolerance = 10,
    int iResults = 1,
    int iCenterPOS = 1,
    float fMinScale = 1.0f,
    float fMaxScale = 1.0f,
    float fScaleStep = 0.1f,
    int iReturnDebug = 0,
    int iUseCache = 0
);
```

**Use Cases:**
- Offline image analysis
- Batch processing of screenshots
- Testing without screen dependency

---

#### ImageSearch_hBitmap
Search using HBITMAP handles (for advanced scenarios).

**C++ Signature:**
```cpp
const wchar_t* WINAPI ImageSearch_hBitmap(
    HBITMAP hBitmapSource,        // Source bitmap handle
    HBITMAP hBitmapTarget,        // Target bitmap handle
    int iTolerance,
    int iCenterPOS,
    float fMinScale,
    float fMaxScale,
    float fScaleStep
);
```

---

### Utility Functions

#### ImageSearch_CaptureScreen
Capture a screen region as HBITMAP.

**C++ Signature:**
```cpp
HBITMAP WINAPI ImageSearch_CaptureScreen(
    int iLeft = 0,
    int iTop = 0,
    int iRight = 0,    // 0 = full screen width
    int iBottom = 0,   // 0 = full screen height
    int iScreen = 0    // Monitor index
);
```

---

#### ImageSearch_hBitmapLoad
Load image file as HBITMAP with transparent color support.

**C++ Signature:**
```cpp
HBITMAP WINAPI ImageSearch_hBitmapLoad(
    const wchar_t* sImageFile,
    int iAlpha = 0,    // Alpha transparency (0-255, 0 = no transparency)
    int iRed = 0,      // Transparent color RGB components
    int iGreen = 0,
    int iBlue = 0
);
```

---

#### ImageSearch_MouseClick
Perform mouse click with smooth movement.

**C++ Signature:**
```cpp
int WINAPI ImageSearch_MouseClick(
    const wchar_t* sButton,  // "left", "right", "middle"
    int iX,                  // X coordinate (-1 = current position)
    int iY,                  // Y coordinate (-1 = current position)
    int iClicks,             // Number of clicks (1, 2, ...)
    int iSpeed,              // Movement speed 0-100 (0 = instant)
    int iScreen              // Monitor index
);
```

---

#### ImageSearch_MouseMove
Move mouse cursor with smooth animation.

**C++ Signature:**
```cpp
int WINAPI ImageSearch_MouseMove(
    int iX,
    int iY,
    int iSpeed,      // 0-100 (0 = instant)
    int iScreen      // Monitor index
);
```

---

#### ImageSearch_MouseClickWin
Click inside a specific window (by title/text).

**C++ Signature:**
```cpp
int WINAPI ImageSearch_MouseClickWin(
    const wchar_t* sTitle,   // Window title
    const wchar_t* sText,    // Window text
    int iX,
    int iY,
    const wchar_t* sButton,
    int iClicks,
    int iSpeed
);
```

---

#### ImageSearch_ClearCache
Clear all cached search results and bitmaps.

**C++ Signature:**
```cpp
void WINAPI ImageSearch_ClearCache();
```

---

#### ImageSearch_GetVersion
Get DLL version string.

**C++ Signature:**
```cpp
const wchar_t* WINAPI ImageSearch_GetVersion();
```

**Returns:** `"ImageSearchDLL v3.3 [x64] 2025.10.15  ::  Dao Van Trong - TRONG.PRO"`

---

#### ImageSearch_GetSysInfo
Get system information (CPU features, memory, etc.).

**C++ Signature:**
```cpp
const wchar_t* WINAPI ImageSearch_GetSysInfo();
```

---

## 📝 AutoIt Examples

### Basic Usage

#### Example 1: Simple Image Search
```autoit
#include "ImageSearchDLL_UDF.au3"

; Initialize the library
_ImageSearch_Startup()

; Search for a single image on the screen
Local $sResult = _ImageSearch("target.png")

If @error Then
    ConsoleWrite("Error: " & @error & @CRLF)
Else
    ; Parse result
    Local $aResult = _ImageSearch_ParseResult($sResult)
    If $aResult[0] > 0 Then
        ConsoleWrite("Found at: X=" & $aResult[1] & " Y=" & $aResult[2] & @CRLF)
    Else
        ConsoleWrite("Image not found" & @CRLF)
    EndIf
EndIf

; Cleanup
_ImageSearch_Shutdown()
```

---

#### Example 2: Search with Tolerance
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Search with 20% color tolerance
Local $sResult = _ImageSearch("button.png", 0, 0, 0, 0, 0, 20)

Local $aResult = _ImageSearch_ParseResult($sResult)
If $aResult[0] > 0 Then
    ConsoleWrite("Button found at: " & $aResult[1] & ", " & $aResult[2] & @CRLF)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 3: Multi-Scale Search
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Search for image scaled from 80% to 120% with 10% increments
Local $sResult = _ImageSearch("icon.png", _
    0, 0, 0, 0, _          ; Full screen
    0, _                    ; Primary monitor
    10, _                   ; Tolerance
    1, _                    ; First match only
    1, _                    ; Return center coordinates
    0.8, 1.2, 0.1)         ; Scale: min=0.8, max=1.2, step=0.1

Local $aResult = _ImageSearch_ParseResult($sResult)
If $aResult[0] > 0 Then
    ConsoleWrite("Found at scale: " & $aResult[5] & @CRLF)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 4: Find All Matches
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Find all occurrences of an image
Local $sResult = _ImageSearch("icon.png", 0, 0, 0, 0, 0, 10, 100)

Local $aResult = _ImageSearch_ParseResult($sResult)
ConsoleWrite("Found " & $aResult[0] & " matches" & @CRLF)

; Loop through all matches
For $i = 1 To $aResult[0]
    Local $iX = $aResult[$i * 6 - 5]
    Local $iY = $aResult[$i * 6 - 4]
    ConsoleWrite("Match " & $i & ": (" & $iX & ", " & $iY & ")" & @CRLF)
Next

_ImageSearch_Shutdown()
```

---

#### Example 5: Search in Specific Region
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Search only in the top-left quadrant of the screen
Local $iLeft = 0
Local $iTop = 0
Local $iRight = @DesktopWidth / 2
Local $iBottom = @DesktopHeight / 2

Local $sResult = _ImageSearch("target.png", $iLeft, $iTop, $iRight, $iBottom)

Local $aResult = _ImageSearch_ParseResult($sResult)
If $aResult[0] > 0 Then
    ConsoleWrite("Found in region!" & @CRLF)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 6: Click on Found Image
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Search for button and click it
Local $sResult = _ImageSearch("button.png")
Local $aResult = _ImageSearch_ParseResult($sResult)

If $aResult[0] > 0 Then
    Local $iX = $aResult[1]
    Local $iY = $aResult[2]
    
    ; Click at the center of found image with smooth movement
    _ImageSearch_MouseClick("left", $iX, $iY, 1, 50)
    ConsoleWrite("Clicked button at: " & $iX & ", " & $iY & @CRLF)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 7: Wait for Image to Appear
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Wait up to 10 seconds for image to appear
Local $iTimeout = 10000  ; milliseconds
Local $iStartTime = TimerInit()
Local $bFound = False

While TimerDiff($iStartTime) < $iTimeout
    Local $sResult = _ImageSearch("loading_complete.png")
    Local $aResult = _ImageSearch_ParseResult($sResult)
    
    If $aResult[0] > 0 Then
        $bFound = True
        ConsoleWrite("Image appeared after " & TimerDiff($iStartTime) & " ms" & @CRLF)
        ExitLoop
    EndIf
    
    Sleep(100)  ; Check every 100ms
WEnd

If Not $bFound Then
    ConsoleWrite("Timeout: Image did not appear" & @CRLF)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 8: Search Multiple Images
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Search for multiple images (whichever appears first)
Local $sImageList = "button1.png|button2.png|button3.png"
Local $sResult = _ImageSearch($sImageList)

Local $aResult = _ImageSearch_ParseResult($sResult)
If $aResult[0] > 0 Then
    Local $sFoundImage = $aResult[6]  ; Source image that was found
    ConsoleWrite("Found: " & $sFoundImage & @CRLF)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 9: Image-in-Image Search
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Search for pattern within a saved screenshot
Local $sResult = _ImageSearch_InImage("screenshot.png", "target.png")

Local $aResult = _ImageSearch_ParseResult($sResult)
If $aResult[0] > 0 Then
    ConsoleWrite("Found at relative position: " & $aResult[1] & ", " & $aResult[2] & @CRLF)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 10: Using Cache for Performance
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Enable caching for repeated searches
Local $sResult = _ImageSearch("icon.png", 0, 0, 0, 0, 0, 10, 1, 1, 1.0, 1.0, 0.1, 0, 1)

; Subsequent searches will be faster
For $i = 1 To 100
    $sResult = _ImageSearch("icon.png", 0, 0, 0, 0, 0, 10, 1, 1, 1.0, 1.0, 0.1, 0, 1)
    Sleep(100)
Next

; Clear cache when done
_ImageSearch_ClearCache()

_ImageSearch_Shutdown()
```

---

#### Example 11: Multi-Monitor Support
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Search on second monitor only
Local $iMonitor = 2
Local $sResult = _ImageSearch("target.png", 0, 0, 0, 0, $iMonitor)

Local $aResult = _ImageSearch_ParseResult($sResult)
If $aResult[0] > 0 Then
    ; Click on found image (coordinates are absolute)
    _ImageSearch_MouseClick("left", $aResult[1], $aResult[2], 1, 50, 0)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 12: Screen Capture
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Capture specific region
Local $hBitmap = _ImageSearch_CaptureScreen(100, 100, 500, 400)

If $hBitmap Then
    ; Save captured bitmap to file
    _GDIPlus_Startup()
    Local $hImage = _GDIPlus_BitmapCreateFromHBITMAP($hBitmap)
    _GDIPlus_ImageSaveToFile($hImage, @ScriptDir & "\captured.png")
    _GDIPlus_ImageDispose($hImage)
    _GDIPlus_Shutdown()
    
    _WinAPI_DeleteObject($hBitmap)
    ConsoleWrite("Screen captured successfully" & @CRLF)
EndIf

_ImageSearch_Shutdown()
```

---

#### Example 13: Get System Information
```autoit
#include "ImageSearchDLL_UDF.au3"

_ImageSearch_Startup()

; Display DLL version and system info
Local $sVersion = _ImageSearch_GetVersion()
Local $sSysInfo = _ImageSearch_GetSysInfo()

ConsoleWrite("Version: " & $sVersion & @CRLF)
ConsoleWrite("System: " & $sSysInfo & @CRLF)

_ImageSearch_Shutdown()
```

---

## ⚙️ Performance Optimization

### SIMD Acceleration
The DLL automatically detects and uses the best available SIMD instruction set:
- **AVX512** - 512-bit vectors (Intel Skylake-X+)
- **AVX2** - 256-bit vectors (Intel Haswell+, AMD Ryzen+)
- **SSE2** - 128-bit vectors (All modern CPUs)

### Caching System
Two-tier caching dramatically improves performance for repeated searches:

1. **Memory Cache** - LRU cache with 100 bitmap slots
2. **Disk Cache** - Persistent cache in `%TEMP%` directory

**Cache Key Format:** `normalized_path|tolerance|transparent|scale`

Enable caching for best performance:
```autoit
Local $sResult = _ImageSearch("icon.png", 0, 0, 0, 0, 0, 10, 1, 1, 1.0, 1.0, 0.1, 0, 1)
```

---

## 🔧 Error Handling

### Error Codes

| Code | Constant | Description |
|------|----------|-------------|
| -1 | `IMGSE_INVALID_PATH` | Invalid path or image format |
| -2 | `IMGSE_FAILED_TO_LOAD_IMAGE` | Failed to load image from file |
| -3 | `IMGSE_FAILED_TO_GET_SCREEN_DC` | Failed to get screen device context |
| -4 | `IMGSE_INVALID_SEARCH_REGION` | Invalid search region specified |
| -5 | `IMGSE_INVALID_PARAMETERS` | Invalid parameters provided |
| -6 | `IMGSE_INVALID_SOURCE_BITMAP` | Invalid source bitmap |
| -7 | `IMGSE_INVALID_TARGET_BITMAP` | Invalid target bitmap |
| -9 | `IMGSE_RESULT_TOO_LARGE` | Result string too large |
| -10 | `IMGSE_INVALID_MONITOR` | Invalid monitor index |

### Error Handling Example
```autoit
Local $sResult = _ImageSearch("nonexistent.png")
Local $aResult = _ImageSearch_ParseResult($sResult)

If $aResult[0] < 0 Then
    Switch $aResult[0]
        Case -1
            ConsoleWrite("Error: Invalid image file" & @CRLF)
        Case -2
            ConsoleWrite("Error: Failed to load image" & @CRLF)
        Case Else
            ConsoleWrite("Error code: " & $aResult[0] & @CRLF)
    EndSwitch
EndIf
```

---

## 🤝 Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 👨 💻 Author

**Dao Van Trong - TRONG.PRO**

For support, bug reports, or feature requests, please open an issue on GitHub.

---

## ☕ Support My Work

Enjoy my work? [Buy me a 🍻](https://buymeacoffee.com/trong) or tip via ❤️ [PayPal](https://paypal.me/DaoVanTrong)

Your support helps me continue developing and maintaining this library for the community! 🙏

---

## 🌟 Acknowledgments

- GDI+ for image processing
- Intel Intrinsics Guide for SIMD optimizations
- AutoIt community for testing and feedback

---

**Happy Automating! 🚀**

