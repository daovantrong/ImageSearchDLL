# ImageSearch UDF - AutoIt Wrapper for ImageSearchDLL

## Overview

ImageSearchDLL_UDF.au3 is a high-level AutoIt wrapper for ImageSearchDLL, providing easy-to-use functions for image searching, mouse automation, and multi-monitor support. It handles all the complexity of DLL calls and provides a clean, reliable API with built-in error handling and fallback mechanisms.

- **Author**: Dao Van Trong - TRONG.PRO
- **UDF Version**: v3.3 
- **Compatible with**: ImageSearchDLL v3.3+
- **AutoIt Version**: 3.3.16.1+
- **License**: MIT License

## ☕ Support My Work

Enjoy my work? [Buy me a 🍻](https://buymeacoffee.com/trong) or tip via ❤️ [PayPal](https://paypal.me/DaoVanTrong)

Your support helps me continue developing and maintaining this library for the community! 🙏

## Key Features

### Search Functions
- **Screen Search**: Find images on screen with multi-monitor support
- **Image-in-Image Search**: Find images within other images
- **HBITMAP Search**: Direct bitmap handle searching
- **Wait & Click**: Wait for image and auto-click when found
- **Cache Control**: Enable/disable persistent caching per search

### Mouse Automation (v3.3 Enhanced)
- **Multi-Monitor Support**: 100% reliable on all monitor configurations
- **Negative Coordinates**: Full support via WinAPI `SetCursorPos`
- **Click Simulation**: WinAPI `mouse_event` for all button types
- **Smooth Movement**: Optional speed parameter for cursor animation
- **Window Clicks**: Click relative to window positions

### Monitor Management
- **Auto-Detection**: Enumerate all connected monitors
- **Virtual Desktop**: Coordinate conversion between monitor and virtual space
- **Specific Monitor Search**: 2-3x faster when searching single monitor
- **Monitor Info**: Get position, size, and primary status

## Installation

1. **Include the UDF** in your AutoIt script:
```autoit
#include "ImageSearchDLL_UDF.au3"
```

2. **Place DLL** in same directory as script:
   - `ImageSearchDLL_x64.dll` for x64 AutoIt
   - `ImageSearchDLL_x86.dll` for x86 AutoIt

3. **Initialize** (automatic on first use):
```autoit
_ImageSearch_Startup()  ; Optional - auto-called if needed
```

## Quick Start Examples

### Example 1: Simple Image Search
```autoit
#include "ImageSearchDLL_UDF.au3"

; Search for image on screen
Local $aResult = _ImageSearch("button.png")

If $aResult[0][0] > 0 Then
    ConsoleWrite("Found at: X=" & $aResult[1][0] & ", Y=" & $aResult[1][1] & @CRLF)
    ; Click the found image
    _ImageSearch_MouseClick("left", $aResult[1][0], $aResult[1][1])
Else
    ConsoleWrite("Image not found" & @CRLF)
EndIf
```

### Example 2: Multi-Monitor Search
```autoit
#include "ImageSearchDLL_UDF.au3"

; Search on all monitors (virtual desktop)
Local $aResult = _ImageSearch("icon.png", 0, 0, 0, 0, -1)

; OR search on specific monitor (faster!)
Local $aResult = _ImageSearch("icon.png", 0, 0, 0, 0, 2) ; Monitor 2

If $aResult[0][0] > 0 Then
    ConsoleWrite("Found " & $aResult[0][0] & " match(es)" & @CRLF)
    For $i = 1 To $aResult[0][0]
        ConsoleWrite("Match " & $i & ": X=" & $aResult[$i][0] & ", Y=" & $aResult[$i][1] & @CRLF)
    Next
EndIf
```

### Example 3: Wait and Click
```autoit
#include "ImageSearchDLL_UDF.au3"

; Wait up to 5 seconds for image, then click it
Local $iResult = _ImageSearch_WaitClick(5000, "submit.png", "left", 1)

If $iResult Then
    ConsoleWrite("Image found and clicked!" & @CRLF)
Else
    ConsoleWrite("Timeout - image not found" & @CRLF)
EndIf
```

### Example 4: Find All Occurrences
```autoit
#include "ImageSearchDLL_UDF.au3"

; Find all matches (up to 10)
Local $aResult = _ImageSearch("item.png", 0, 0, 0, 0, -1, 10, 10)

If $aResult[0][0] > 0 Then
    For $i = 1 To $aResult[0][0]
        ConsoleWrite("Match " & $i & ": ")
        ConsoleWrite("X=" & $aResult[$i][0] & ", Y=" & $aResult[$i][1])
        ConsoleWrite(", W=" & $aResult[$i][2] & ", H=" & $aResult[$i][3] & @CRLF)
    Next
EndIf
```

### Example 5: Image-in-Image Search
```autoit
#include "ImageSearchDLL_UDF.au3"

; Search for button.png within screenshot.png
Local $aResult = _ImageSearch_InImage("screenshot.png", "button.png", 15, 5)

If $aResult[0][0] > 0 Then
    ConsoleWrite("Found " & $aResult[0][0] & " match(es) in image" & @CRLF)
EndIf
```

### Example 6: Region Search with Tolerance
```autoit
#include "ImageSearchDLL_UDF.au3"

; Search in specific region with high tolerance
Local $aResult = _ImageSearch("target.png", 100, 100, 800, 600, -1, 20)

If $aResult[0][0] > 0 Then
    ConsoleWrite("Found at: " & $aResult[1][0] & ", " & $aResult[1][1] & @CRLF)
EndIf
```

### Example 7: Cache-Enabled Search
```autoit
#include "ImageSearchDLL_UDF.au3"

; Enable cache for 30-50% performance boost on repeated searches
Local $aResult = _ImageSearch("icon.png", 0, 0, 0, 0, -1, 10, 1, 1, 1.0, 1.0, 0.1, 0, 1)
;                                                                                       ↑
;                                                                               iUseCache=1

If $aResult[0][0] > 0 Then
    ConsoleWrite("Found (cached search): " & $aResult[1][0] & ", " & $aResult[1][1] & @CRLF)
EndIf
```

### Example 8: Monitor Management
```autoit
#include "ImageSearchDLL_UDF.au3"

; Get monitor information
_ImageSearch_Monitor_GetList()

ConsoleWrite("Total monitors: " & $g_aMonitorList[0][0] & @CRLF)

For $i = 1 To $g_aMonitorList[0][0]
    ConsoleWrite("Monitor " & $i & ": " & _
        $g_aMonitorList[$i][5] & "x" & $g_aMonitorList[$i][6] & _
        ($g_aMonitorList[$i][7] ? " (Primary)" : "") & @CRLF)
Next

; Convert coordinates between monitor and virtual desktop
Local $aVirtual = _ImageSearch_Monitor_ToVirtual(2, 100, 200)
ConsoleWrite("Monitor 2 (100,200) = Virtual (" & $aVirtual[0] & "," & $aVirtual[1] & ")" & @CRLF)
```

## Core Functions Reference

### Search Functions

#### _ImageSearch()
```autoit
_ImageSearch($sImagePath, [$iLeft=0], [$iTop=0], [$iRight=0], [$iBottom=0], 
             [$iScreen=-1], [$iTolerance=10], [$iResults=1], [$iCenterPOS=1], 
             [$fMinScale=1.0], [$fMaxScale=1.0], [$fScaleStep=0.1], 
             [$iReturnDebug=0], [$iUseCache=0])
```
**Parameters:**
- `$sImagePath` - Image file path (or multiple: "img1.png|img2.png")
- `$iLeft, $iTop, $iRight, $iBottom` - Search region (0 = full screen)
- `$iScreen` - Monitor: -1=all, 0=primary, 1=first, 2=second, etc.
- `$iTolerance` - Color tolerance 0-255
- `$iResults` - Max results to return (1-64)
- `$iCenterPOS` - 1=return center, 0=return top-left
- `$fMinScale, $fMaxScale, $fScaleStep` - Scaling parameters
- `$iReturnDebug` - 1=enable debug output
- `$iUseCache` - 1=enable cache, 0=disable

**Returns:**
- Array[0][0] = match count
- Array[1..n][0] = X coordinate
- Array[1..n][1] = Y coordinate  
- Array[1..n][2] = Width
- Array[1..n][3] = Height

#### _ImageSearch_InImage()
```autoit
_ImageSearch_InImage($sSourceImage, $sTargetImage, [$iTolerance=10], 
                     [$iResults=1], [$iCenterPOS=1], [$fMinScale=1.0], 
                     [$fMaxScale=1.0], [$fScaleStep=0.1], [$iReturnDebug=0], 
                     [$iUseCache=0])
```
Search for target image(s) within a source image file.

#### _ImageSearch_Wait()
```autoit
_ImageSearch_Wait($iTimeout, $sImagePath, [$iLeft=0], [$iTop=0], 
                  [$iRight=0], [$iBottom=0], [$iScreen=-1], [$iTolerance=10], 
                  [$iResults=1], [$iCenterPOS=1], [$fMinScale=1.0], 
                  [$fMaxScale=1.0], [$fScaleStep=0.1], [$iReturnDebug=0], 
                  [$iUseCache=0])
```
Wait for image to appear (with timeout in milliseconds).

#### _ImageSearch_WaitClick()
```autoit
_ImageSearch_WaitClick($iTimeout, $sImagePath, [$sButton="left"], 
                       [$iClicks=1], [$iLeft=0], [$iTop=0], [$iRight=0], 
                       [$iBottom=0], [$iScreen=-1], [$iTolerance=10], 
                       [$iResults=1], [$iCenterPOS=1], [$fMinScale=1.0], 
                       [$fMaxScale=1.0], [$fScaleStep=0.1], [$iReturnDebug=0], 
                       [$iUseCache=0])
```
Wait for image and automatically click it when found.

### Mouse Functions (v3.3 Enhanced)

#### _ImageSearch_MouseMove()
```autoit
_ImageSearch_MouseMove($iX, $iY, [$iSpeed=0], [$iScreen=-1])
```
Move mouse cursor to coordinates. **Supports negative coordinates** for multi-monitor.
- Uses WinAPI `SetCursorPos` for 100% reliability
- `$iSpeed` - 0=instant, >0=smooth movement with steps

#### _ImageSearch_MouseClick()
```autoit
_ImageSearch_MouseClick([$sButton="left"], [$iX=-1], [$iY=-1], 
                        [$iClicks=1], [$iSpeed=0], [$iScreen=-1])
```
Click mouse at coordinates. **Supports negative coordinates** for multi-monitor.
- Uses WinAPI `mouse_event` for reliable clicking
- `$sButton` - "left", "right", "middle"
- `$iX, $iY` - Virtual desktop coordinates (-1 = current position)

#### _ImageSearch_MouseClickWin()
```autoit
_ImageSearch_MouseClickWin($sTitle, $sText, $iX, $iY, 
                           [$sButton="left"], [$iClicks=1], [$iSpeed=0])
```
Click at window-relative coordinates.

### Monitor Functions

#### _ImageSearch_Monitor_GetList()
```autoit
_ImageSearch_Monitor_GetList()
```
Enumerate all monitors and populate `$g_aMonitorList` array.

**Global Array Structure:**
```
$g_aMonitorList[0][0] = Total monitor count
$g_aMonitorList[i][0] = Handle
$g_aMonitorList[i][1] = Left
$g_aMonitorList[i][2] = Top
$g_aMonitorList[i][3] = Right
$g_aMonitorList[i][4] = Bottom
$g_aMonitorList[i][5] = Width
$g_aMonitorList[i][6] = Height
$g_aMonitorList[i][7] = IsPrimary (1/0)
$g_aMonitorList[i][8] = Device name
```

#### _ImageSearch_Monitor_ToVirtual()
```autoit
_ImageSearch_Monitor_ToVirtual($iMonitor, $iX, $iY)
```
Convert monitor-relative coordinates to virtual desktop coordinates.

#### _ImageSearch_Monitor_FromVirtual()
```autoit
_ImageSearch_Monitor_FromVirtual($iMonitor, $iX, $iY)
```
Convert virtual desktop coordinates to monitor-relative coordinates.

### Utility Functions

#### _ImageSearch_CaptureScreen()
```autoit
_ImageSearch_CaptureScreen([$iLeft=0], [$iTop=0], [$iRight=0], 
                           [$iBottom=0], [$iScreen=-1])
```
Capture screen region as HBITMAP handle.

#### _ImageSearch_hBitmapLoad()
```autoit
_ImageSearch_hBitmapLoad($sImageFile, [$iAlpha=0], [$iRed=0], 
                         [$iGreen=0], [$iBlue=0])
```
Load image file as HBITMAP with optional background color.

#### _ImageSearch_GetVersion()
```autoit
_ImageSearch_GetVersion()
```
Get DLL version string.

#### _ImageSearch_GetSysInfo()
```autoit
_ImageSearch_GetSysInfo()
```
Get system info (CPU, screen, cache stats).

#### _ImageSearch_ClearCache()
```autoit
_ImageSearch_ClearCache()
```
Clear all DLL caches (location and bitmap).

## Performance Tips

### Cache System
- **Enable caching** for repeated searches: `$iUseCache=1`
- **30-50% faster** on subsequent searches
- **Persistent** across script runs
- **Auto-validated** (removes stale entries)

### Multi-Monitor Optimization
- Use **specific monitor** (`$iScreen=1` or `2`) for **2-3x faster** search
- Use **virtual desktop** (`$iScreen=-1`) only when needed
- Coordinates are always in virtual desktop space (may be negative)

### Search Optimization
- **Smaller region** = faster search
- **Higher tolerance** = faster but less accurate
- **Fewer results** (`$iResults=1`) = faster
- **Disable debug** in production (`$iReturnDebug=0`)

## Version 3.3 Improvements

### Fixed Issues
✅ **Multi-Monitor Mouse Movement** - Now 100% reliable using WinAPI
✅ **Negative Coordinates** - Full support for monitors positioned left/above primary
✅ **Mouse Click Reliability** - WinAPI `mouse_event` never fails
✅ **Coordinate Conversion** - Proper handling of virtual desktop space

### Enhanced Features
- Debug logging shows actual mouse position after move
- Better error handling with meaningful error codes
- All mouse functions bypass DLL for maximum reliability
- Smooth cursor movement with customizable speed

### Breaking Changes
- Mouse functions no longer rely on DLL implementation
- Always use WinAPI for mouse operations (more reliable)

## Troubleshooting

### "DLL not found" Error
- Ensure DLL is in same directory as script
- Use correct architecture (x64 vs x86)
- Check with `FileExists($g_sImgSearchDLL_Path)`

### Mouse Not Moving on Second Monitor
- ✅ Fixed in v3.3! Now uses WinAPI `SetCursorPos`
- Coordinates are virtual desktop (may be negative)
- Update to UDF v3.3 or later

### Image Not Found
- Check image file exists
- Increase tolerance (`$iTolerance=20`)
- Enable debug (`$iReturnDebug=1`) to see search info
- Try different region/monitor settings

### Slow Search Performance
- Enable cache (`$iUseCache=1`)
- Use specific monitor instead of all monitors
- Reduce search region
- Lower max results

## Example: Complete Script
```autoit
#include "ImageSearchDLL_UDF.au3"

; Initialize
_ImageSearch_Startup()

; Show system info
ConsoleWrite("UDF: " & $IMGS_UDF_VERSION & @CRLF)
ConsoleWrite("DLL: " & _ImageSearch_GetVersion() & @CRLF)
ConsoleWrite(_ImageSearch_GetSysInfo() & @CRLF)

; Search for image with cache enabled
Local $aResult = _ImageSearch("target.png", 0, 0, 0, 0, -1, 10, 5, 1, 1.0, 1.0, 0.1, 1, 1)

If $aResult[0][0] > 0 Then
    ConsoleWrite("Found " & $aResult[0][0] & " match(es):" & @CRLF)
    
    For $i = 1 To $aResult[0][0]
        ConsoleWrite("  Match " & $i & ": ")
        ConsoleWrite("X=" & $aResult[$i][0] & ", Y=" & $aResult[$i][1])
        ConsoleWrite(", W=" & $aResult[$i][2] & ", H=" & $aResult[$i][3] & @CRLF)
        
        ; Move mouse and click
        _ImageSearch_MouseMove($aResult[$i][0], $aResult[$i][1], 10)
        Sleep(500)
        _ImageSearch_MouseClick("left", $aResult[$i][0], $aResult[$i][1])
        Sleep(1000)
    Next
Else
    ConsoleWrite("No matches found" & @CRLF)
EndIf

; Cleanup
_ImageSearch_Shutdown()
```

## License & Contact

- **Author**: Dao Van Trong
- **Website**: TRONG.PRO
- **Email**: trong@email.com
- **License**: MIT License
- **AutoIt Forum**: Post in AutoIt General Help

## See Also

- **README.md** - DLL API reference and C++ examples
- **ImageSearch TEST Suite.au3** - Interactive GUI test application

---

Thank you for using ImageSearch UDF! 🚀
