#RequireAdmin
#Region ;**** Directives created by AutoIt3Wrapper_GUI ****
;~ #AutoIt3Wrapper_UseX64=y
#EndRegion ;**** Directives created by AutoIt3Wrapper_GUI ****
;~ #pragma compile(x64, true) ; false
#cs ----------------------------------------------------------------------------
	;
	;    Title .........: ImageSearch Test Suite v3.3
	;    AutoIt Version : 3.3.16.1+
	;    Author ........: Dao Van Trong (TRONG.PRO)
	;    UDF Version ...: 3.3
	;
	;    Note ..........: This script is a graphical user interface (GUI) front-end for the
	;                     ImageSearchDLL_UDF.au3 and its underlying ImageSearch.dll.
	;                     It allows for visual configuration of image search tasks on the screen
	;                     or within another image file, and now includes a file browser for
	;                     selecting existing images.
	;
	; -------------------------------------------------------------------------------------------------------------------------------
	; #SECTION# SCRIPT OVERVIEW
	; -------------------------------------------------------------------------------------------------------------------------------
	;
	; This enhanced script provides a powerful and user-friendly interface for performing complex image search and automation tasks.
	; It acts as a control panel for the high-performance ImageSearch UDF, allowing you to visually configure, execute,
	; and log search operations without writing complex code. It supports searching images on the screen or within another image.
	; NEW: Added "Browse" buttons to select existing image files instead of only capturing from screen.
	;
	; -------------------------------------------------------------------------------------------------------------------------------
	; #SECTION# FIRST-TIME SETUP
	; -------------------------------------------------------------------------------------------------------------------------------
	;
	; Before you can start a search, you need to provide the images you want to find.
	;
	; 1. RUN THE SCRIPT: The main window will appear. On the right side, you will see 12 empty "Image Target" slots.
	;
	; 2. CREATE OR SELECT AN IMAGE:
	;    - Click the "Create" button next to slot #1 to capture a region from the screen.
	;    - OR click the "Browse" button next to slot #1 to select an existing image file.
	;
	; 3. FOR CREATE: The script window will hide. Your mouse cursor will turn into a crosshair.
	;    Click and drag a rectangle around the object on the screen you want to find. When you release the mouse button,
	;    a bitmap image named "Search_1.bmp" will be saved in the same directory as the script.
	;
	; 4. FOR BROWSE: A file dialog will open allowing you to select BMP, JPG, PNG, or other image files.
	;    The selected image will be copied to the script directory as "Search_X.bmp".
	;
	; 5. PREVIEW UPDATES: The image you just captured/selected will now appear in the preview panel for that slot.
	;
	; 6. REPEAT: Repeat this process for any other images you need to find (up to 12).
	;
	; 7. IMAGE-IN-IMAGE SEARCH: To search within an image, select "Search in Image" mode, then specify a source image file
	;    using the "Browse Source Image" button.
	;
#ce ----------------------------------------------------------------------------

; === INCLUDES ===
#include <Array.au3>
#include <GDIPlus.au3>
#include <ScreenCapture.au3>
#include <WinAPI.au3>
#include <WindowsConstants.au3>
#include <GUIConstantsEx.au3>
#include <EditConstants.au3>
#include <StaticConstants.au3>
#include <ButtonConstants.au3>
#include <Date.au3>
#include <Misc.au3>
#include <Math.au3>
#include <GuiEdit.au3>
#include <GuiStatusBar.au3>
#include <GuiComboBox.au3>
#include <ComboConstants.au3>
#include <File.au3>
#include "ImageSearchDLL_UDF.au3" ; Core image search functionality v3.3

; === GLOBAL CONSTANTS ===
Global Const $MAX_IMAGES = 12 ; Maximum number of image targets the GUI supports.
Global Const $g_sImgEmptySlots = @TempDir & "\" & 'Default_UIF.jpg' ; Default image to show in empty slots.
Global Const $g_sPlaceholderPath = _Deploy_ImgEmptySlots() ? $g_sImgEmptySlots : @WindowsDir & "\Web\Wallpaper\Windows\img0.jpg"

; === GLOBAL VARIABLES ===
Global $g_asImagePaths[$MAX_IMAGES] ; Array to store the file paths for the 12 target images.
Global $g_nMsg ; Stores the message code from GUIGetMsg() for the main event loop.
Global $g_hMainGUI ; Handle for the main GUI window.
Global $g_hLog ; Handle for the activity log Edit control.
Global $g_hStatusBar ; Handle for the status bar at the bottom of the GUI.

; --- GUI Control IDs ---
Global $g_idBtnStart, $g_idBtnSelectAll, $g_idBtnDeselectAll, $g_idBtnSelectArea, $g_idSystemInfo
Global $g_idInputDelay, $g_idChkMoveMouse
Global $g_idRadNoClick, $g_idRadSingleClick, $g_idRadDoubleClick
Global $g_idChkWait, $g_idInputWaitTime
Global $g_idChkUseArea, $g_idInputLeft, $g_idInputTop, $g_idInputRight, $g_idInputBottom
Global $g_idChkMultiSearch, $g_idChkFindAll, $g_idChkUseTolerance, $g_idInputTolerance, $g_idChkEnableDebug, $g_idChkUseCache
Global $g_aidPic[$MAX_IMAGES], $g_aidChkSearch[$MAX_IMAGES], $g_aidBtnCreate[$MAX_IMAGES], $g_aidBtnBrowse[$MAX_IMAGES]
Global $g_idRadSearchOnScreen, $g_idRadSearchInImage, $g_idInputSourceImage, $g_idBtnBrowseSource
; NEW v3.3: Monitor selection controls
Global $g_idChkUseSpecificMonitor, $g_idComboMonitor
Global $g_iMonitorCount = 0

_Main()

; #FUNCTION# ====================================================================================================================
; Name...........: _Main
; Description....: Main program entry point. Initializes all necessary components and enters the GUI message loop to handle user interactions.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _Main()
	; Explicitly initialize the ImageSearch library. This is a critical first step.
	If Not _ImageSearch_Startup() Then
		MsgBox(16, "Fatal Error", "Failed to initialize the ImageSearch DLL. @error: " & @error & @CRLF & "The script will now exit.")
		Exit
	EndIf

	; Start GDI+ for image processing and create the GUI
	_GDIPlus_Startup()
	_InitializeImagePaths()
	_EnumerateMonitors() ; NEW: Get monitor list using UDF
	_CreateGUI()
	_UpdateAllImagePreviews()
	_RefreshImageTooltips()

	; Main event loop to handle GUI events. The script will wait here for user input.
	While 1
		$g_nMsg = GUIGetMsg()
		Switch $g_nMsg
			Case $GUI_EVENT_CLOSE
				Exit ; Exit the loop and terminate the script.

			Case $g_idRadSearchInImage, $g_idRadSearchOnScreen
				_UpdateSearchModeControls()

			Case $g_idChkUseSpecificMonitor
				_UpdateMonitorControls()

			Case $g_idBtnStart
				_ExecuteSearch() ; Begin the image search process.

			Case $g_idBtnSelectAll
				_SelectAll(True) ; Check all image target checkboxes.

			Case $g_idBtnDeselectAll
				_SelectAll(False) ; Uncheck all image target checkboxes.

			Case $g_idBtnSelectArea
				_SelectAreaOnScreen() ; Allow user to define a search area on the screen.

			Case $g_idBtnBrowseSource
				_BrowseSourceImage() ; Open file dialog to select the source image for image-in-image search.

				; Event handlers for the "Create" and "Browse" buttons for each image slot.
			Case $g_aidBtnCreate[0] To $g_aidBtnCreate[$MAX_IMAGES - 1]
				_HandleImageCreation($g_nMsg)

			Case $g_aidBtnBrowse[0] To $g_aidBtnBrowse[$MAX_IMAGES - 1]
				_HandleImageBrowse($g_nMsg)
		EndSwitch
	WEnd

	_Exit()
EndFunc   ;==>_Main

; #FUNCTION# ====================================================================================================================
; Name...........: _InitializeImagePaths
; Description....: Populates the global array '$g_asImagePaths' with default file paths for the search images (e.g., Search_1.bmp, Search_2.bmp).
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _InitializeImagePaths()
	For $i = 0 To $MAX_IMAGES - 1
		$g_asImagePaths[$i] = @ScriptDir & "\Search_" & $i + 1 & ".bmp"
	Next
EndFunc   ;==>_InitializeImagePaths

; #FUNCTION# ====================================================================================================================
; Name...........: _EnumerateMonitors
; Description....: Enumerates all monitors using UDF function and stores count
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _EnumerateMonitors()
	; Use UDF's built-in monitor enumeration
	_ImageSearch_Monitor_GetList()
	$g_iMonitorCount = $g_aMonitorList[0][0]

	If $g_bImageSearch_Debug Then
		ConsoleWrite("Detected " & $g_iMonitorCount & " monitor(s)" & @CRLF)
		For $i = 1 To $g_iMonitorCount
			Local $sInfo = "Monitor " & $i & ": " & $g_aMonitorList[$i][5] & "x" & $g_aMonitorList[$i][6]
			If $g_aMonitorList[$i][7] Then $sInfo &= " (Primary)"
			ConsoleWrite("  " & $sInfo & @CRLF)
		Next
	EndIf
EndFunc   ;==>_EnumerateMonitors

; #FUNCTION# ====================================================================================================================
; Name...........: _PopulateMonitorCombo
; Description....: Populates the monitor selection combobox with detected monitors
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _PopulateMonitorCombo()
	If $g_iMonitorCount = 0 Then Return

	Local $sMonitorList = ""
	For $i = 1 To $g_iMonitorCount
		Local $sLabel = "Monitor " & $i
		If $g_aMonitorList[$i][7] Then $sLabel &= " (Primary)"
		$sLabel &= " - " & $g_aMonitorList[$i][5] & "x" & $g_aMonitorList[$i][6]
		$sMonitorList &= ($i > 1 ? "|" : "") & $sLabel
	Next

	; Set data and select primary monitor by default
	GUICtrlSetData($g_idComboMonitor, $sMonitorList)

	; Find and select primary monitor
	For $i = 1 To $g_iMonitorCount
		If $g_aMonitorList[$i][7] Then
			_GUICtrlComboBox_SetCurSel($g_idComboMonitor, $i - 1)
			Return
		EndIf
	Next
	_GUICtrlComboBox_SetCurSel($g_idComboMonitor, 0)
EndFunc   ;==>_PopulateMonitorCombo

Func _CreateSlot(ByRef $aChk, ByRef $aPic, ByRef $aBtnCreate, ByRef $aBtnBrowse, $idx, $col, $row, _
		$baseLeft, $baseTop, $colSpacing, $rowSpacing, $slotGroupW, $slotGroupH, $slotChkW, $slotChkH, $slotPicW, $slotPicH, $slotBtnW, $slotBtnH)
	Local $leftGroup = $baseLeft + ($col * $colSpacing)
	Local $topGroup = $baseTop + ($row * $rowSpacing)
	GUICtrlCreateGroup("Slot " & ($idx + 1), $leftGroup, $topGroup, $slotGroupW, $slotGroupH)
	Local $ox_chk = 5, $oy_chk = 13
	Local $ox_pic = 10, $oy_pic = 36
	Local $ox_btnCreate = 5, $oy_btnCreate = 142
	Local $ox_btnBrowse = $ox_btnCreate + $slotBtnW + 6
	$aChk[$idx] = GUICtrlCreateCheckbox("Search", $leftGroup + $ox_chk, $topGroup + $oy_chk, $slotChkW, $slotChkH)
	$aPic[$idx] = GUICtrlCreatePic("", $leftGroup + $ox_pic, $topGroup + $oy_pic, $slotPicW, $slotPicH)
	GUICtrlSetState(-1, $GUI_DISABLE)
	$aBtnCreate[$idx] = GUICtrlCreateButton("Create", $leftGroup + $ox_btnCreate, $topGroup + $oy_btnCreate, $slotBtnW, $slotBtnH)
	GUICtrlSetTip(-1, "Capture an image from the screen for slot " & ($idx + 1) & ".")
	$aBtnBrowse[$idx] = GUICtrlCreateButton("Browse", $leftGroup + $ox_btnBrowse, $topGroup + $oy_btnCreate, $slotBtnW, $slotBtnH)
	GUICtrlSetTip(-1, "Select an existing image file for slot " & ($idx + 1) & ".")
	GUICtrlCreateGroup("", -99, -99, 1, 1)
EndFunc   ;==>_CreateSlot

Func _CreateGUI()
	Local $winW = 1024, $winH = 791
	Local $cfgX = 10, $cfgY = 2, $cfgW = 460, $cfgH = 350
	Local $smX = 20, $smY = 22, $smW = 210, $smH = 310
	Local $smInnerX = 30, $smInnerY = 42
	Local $inputSourceW = 196, $inputSourceH = 23
	Local $btnBrowseX = 167, $btnBrowseY = 76, $btnBrowseW = 55, $btnBrowseH = 23
	Local $paramX = 240, $paramY = 22, $paramW = 220, $paramH = 120
	Local $labelW = 90, $labelH = 20
	Local $inputW = 80, $inputH = 23
	Local $actX = 240, $actY = 152, $actW = 220, $actH = 132
	Local $actLabelW = 40, $actLabelH = 20
	Local $actRadioW = 60, $actRadioH = 20
	Local $areaX = 10, $areaY = 356, $areaW = 460, $areaH = 80
	Local $areaChkX = 23, $areaChkY = 373, $areaChkW = 150, $areaChkH = 20
	Local $areaLabelW = 45, $areaLabelH = 20
	Local $areaInputW = 70, $areaInputH = 23
	Local $btnSelectAreaX = 191, $btnSelectAreaY = 373, $btnSelectAreaW = 268, $btnSelectAreaH = 25
	Local $imgX = 474, $imgY = 10, $imgW = 556, $imgH = 546
	Local $slotGroupW = 127, $slotGroupH = 172
	Local $slotChkW = 100, $slotChkH = 20
	Local $slotPicW = 100, $slotPicH = 100
	Local $slotBtnW = 55, $slotBtnH = 22
	Local $logX = 10, $logY = 604, $logW = 1008, $logH = 158
	Local $logEditX = 15, $logEditY = 619, $logEditW = 1000, $logEditH = 136
	Local $sysX = 10, $sysY = 440, $sysW = 460, $sysH = 160
	Local $sysLabelW = 440, $sysLabelH = 20
	Local $sysInfoLabelH = 40
	Local $btnSelectAllX = 477, $btnSelectAllY = 560, $btnSelectAllW = 100, $btnSelectAllH = 38
	Local $btnStartX = 580, $btnStartY = 560, $btnStartW = 324, $btnStartH = 38
	Local $btnDeselectAllX = 912, $btnDeselectAllY = 560, $btnDeselectAllW = 100, $btnDeselectAllH = 38

	; Create main window
	$g_hMainGUI = GUICreate("ImageSearch Automation by Dao Van Trong - TRONG.PRO", $winW, $winH)
	GUISetFont(9, 400, 0, "Segoe UI", $g_hMainGUI, 5)
	GUISetBkColor(0xF3F3F3, $g_hMainGUI)
	GUICtrlCreateGroup("Configuration", $cfgX, $cfgY, $cfgW, $cfgH)
	GUICtrlCreateGroup("Search Mode", $smX, $smY, $smW, $smH)
	$g_idRadSearchOnScreen = GUICtrlCreateRadio("Search on All Screens", $smInnerX, $smInnerY, 180, 20)
	GUICtrlSetState(-1, $GUI_CHECKED)
	GUICtrlSetTip(-1, "Search across all monitors (virtual desktop)")

	; NEW v3.3: Specific monitor selection
	$g_idChkUseSpecificMonitor = GUICtrlCreateCheckbox("Use Specific Monitor", $smInnerX + 10, $smInnerY + 25, 160, 20)
	GUICtrlSetTip(-1, "Search on a specific monitor only (2-3x faster!)")
	$g_idComboMonitor = GUICtrlCreateCombo("", $smInnerX + 10, $smInnerY + 50, 180, 25, BitOR($CBS_DROPDOWNLIST, $CBS_AUTOHSCROLL))
	GUICtrlSetTip(-1, "Select which monitor to search on")
	GUICtrlSetState($g_idComboMonitor, $GUI_DISABLE)
	_PopulateMonitorCombo()

	$g_idRadSearchInImage = GUICtrlCreateRadio("Search in Image", $smInnerX, $smInnerY + 85, 132, 20)
	GUICtrlSetTip(-1, "Search for images within a specified source image file.")
	$g_idInputSourceImage = GUICtrlCreateInput("", $smInnerX, $smInnerY + 115, $inputSourceW, $inputSourceH, BitOR($GUI_SS_DEFAULT_INPUT, $ES_READONLY))
	$g_idBtnBrowseSource = GUICtrlCreateButton("Browse", $btnBrowseX, $smInnerY + 115, $btnBrowseW, $btnBrowseH)
	GUICtrlSetTip(-1, "Select the source image file to search within.")
	$g_idChkMultiSearch = GUICtrlCreateCheckbox("Multi Search (All at once)", $smInnerX, $smInnerY + 150, 200, 20)
	GUICtrlSetTip(-1, "Finds the FIRST occurrence of ANY of the selected images.")
	$g_idChkFindAll = GUICtrlCreateCheckbox("Find All Occurrences", $smInnerX, $smInnerY + 175, 200, 20)
	GUICtrlSetState(-1, $GUI_CHECKED)
	$g_idChkWait = GUICtrlCreateCheckbox("Wait for Image Found", $smInnerX, $smInnerY + 200, 200, 20)
	$g_idChkUseTolerance = GUICtrlCreateCheckbox("Use Tolerance", $smInnerX, $smInnerY + 225, 200, 20)
	GUICtrlSetState(-1, $GUI_CHECKED)
	$g_idChkEnableDebug = GUICtrlCreateCheckbox("Enable DLL Debug", $smInnerX, $smInnerY + 250, 200, 20)
	If Not @Compiled Then GUICtrlSetState(-1, $GUI_CHECKED)

	; NEW v3.3: Cache system
	$g_idChkUseCache = GUICtrlCreateCheckbox("Enable Cache (v3.3)", $smInnerX, $smInnerY + 275, 200, 20)
	GUICtrlSetTip(-1, "Enable persistent cache for 30-50% speed boost on repeated searches")
	GUICtrlCreateGroup("", -99, -99, 1, 1)

	GUICtrlCreateGroup("Parameters", $paramX, $paramY, $paramW, $paramH)
	GUICtrlCreateLabel("Timeout (ms):", $paramX + 10, $paramY + 22, $labelW, $labelH, $SS_RIGHT)
	$g_idInputWaitTime = GUICtrlCreateInput("5000", $paramX + 115, $paramY + 20, $inputW, $inputH)
	GUICtrlCreateLabel("Tolerance:", $paramX + 10, $paramY + 52, $labelW, $labelH, $SS_RIGHT)
	$g_idInputTolerance = GUICtrlCreateInput("10", $paramX + 115, $paramY + 50, $inputW, $inputH)
	GUICtrlCreateLabel("Delay (ms)", $paramX + 10, $paramY + 82, $labelW, $labelH, $SS_RIGHT)
	$g_idInputDelay = GUICtrlCreateInput("500", $paramX + 115, $paramY + 80, $inputW, $inputH)
	GUICtrlCreateGroup("", -99, -99, 1, 1)

	GUICtrlCreateGroup("Actions on Found", $actX, $actY, $actW, $actH)
	$g_idChkMoveMouse = GUICtrlCreateCheckbox("Move Mouse", $actX + 10, $actY + 20, 120, 20)
	GUICtrlSetState(-1, $GUI_CHECKED)
	GUICtrlCreateLabel("Click:", $actX + 10, $actY + 45, $actLabelW, $actLabelH)
	$g_idRadNoClick = GUICtrlCreateRadio("None", $actX + 65, $actY + 45, $actRadioW, $actRadioH)
	GUICtrlSetState(-1, $GUI_CHECKED)
	$g_idRadSingleClick = GUICtrlCreateRadio("Single", $actX + 10, $actY + 70, $actRadioW, $actRadioH)
	$g_idRadDoubleClick = GUICtrlCreateRadio("Double", $actX + 70, $actY + 70, $actRadioW, $actRadioH)
	GUICtrlCreateGroup("", -99, -99, 1, 1)

	GUICtrlCreateGroup("Search Area", $areaX, $areaY, $areaW, $areaH)
	$g_idChkUseArea = GUICtrlCreateCheckbox("Use Custom Area", $areaChkX, $areaChkY, $areaChkW, $areaChkH)
	; All 4 inputs in one row
	GUICtrlCreateLabel("Left:", $areaX + 13, $areaY + 50, $areaLabelW, $areaLabelH)
	$g_idInputLeft = GUICtrlCreateInput("0", $areaX + 58, $areaY + 48, $areaInputW, $areaInputH)
	GUICtrlCreateLabel("Top:", $areaX + 135, $areaY + 50, $areaLabelW, $areaLabelH)
	$g_idInputTop = GUICtrlCreateInput("0", $areaX + 175, $areaY + 48, $areaInputW, $areaInputH)
	GUICtrlCreateLabel("Right:", $areaX + 252, $areaY + 50, $areaLabelW, $areaLabelH)
	$g_idInputRight = GUICtrlCreateInput(@DesktopWidth, $areaX + 297, $areaY + 48, $areaInputW, $areaInputH)
	GUICtrlCreateLabel("Bottom:", $areaX + 374, $areaY + 50, $areaLabelW, $areaLabelH)
	$g_idInputBottom = GUICtrlCreateInput(@DesktopHeight, $areaX + 420, $areaY + 48, $areaInputW, $areaInputH)
	$g_idBtnSelectArea = GUICtrlCreateButton("Select Area on Screen", $btnSelectAreaX, $btnSelectAreaY, $btnSelectAreaW, $btnSelectAreaH)
	GUICtrlCreateGroup("", -99, -99, 1, 1)
	; RIGHT COLUMN: Image Targets group
	GUICtrlCreateGroup("Image Targets", $imgX, $imgY, $imgW, $imgH)
	Local $baseLeft = 479, $baseTop = 27
	Local $colSpacing = 135
	Local $rowSpacing = 172
	Dim $g_aidChkSearch[12], $g_aidPic[12], $g_aidBtnCreate[12], $g_aidBtnBrowse[12]
	Local $idx = 0
	For $r = 0 To 2
		For $c = 0 To 3
			_CreateSlot($g_aidChkSearch, $g_aidPic, $g_aidBtnCreate, $g_aidBtnBrowse, $idx, $c, $r, _
					$baseLeft, $baseTop, $colSpacing, $rowSpacing, _
					$slotGroupW, $slotGroupH, $slotChkW, $slotChkH, $slotPicW, $slotPicH, $slotBtnW, $slotBtnH)
			$idx += 1
		Next
	Next
	; BOTTOM: Activity Log
	GUICtrlCreateGroup("Activity Log", $logX, $logY, $logW, $logH)
	$g_hLog = GUICtrlCreateEdit("", $logEditX, $logEditY, $logEditW, $logEditH, BitOR($ES_MULTILINE, $ES_READONLY, $WS_VSCROLL, $ES_AUTOVSCROLL))
	GUICtrlSetFont(-1, 9, 0, 0, "Segoe UI")
	GUICtrlCreateGroup("", -99, -99, 1, 1)

	; System Information
	GUICtrlCreateGroup("System Information", $sysX, $sysY, $sysW, $sysH)
	GUICtrlCreateLabel("OS: " & @OSVersion & " (" & @OSArch & ")" & "   |   AutoIt: " & @AutoItVersion & (@AutoItX64 ? " (x64)" : ""), $sysX + 10, $sysY + 20, $sysLabelW, $sysLabelH)
	GUICtrlCreateLabel("UDF Version: " & $IMGS_UDF_VERSION & "   |   DLL: " & _ImageSearch_GetVersion(), $sysX + 10, $sysY + 45, $sysLabelW, $sysLabelH)
	GUICtrlCreateLabel("DLL Path: " & $g_sImgSearchDLL_Path, $sysX + 10, $sysY + 70, $sysLabelW, $sysLabelH)
	GUICtrlCreateLabel("Screen: " & @DesktopWidth & "x" & @DesktopHeight & "   |   Monitors: " & $g_iMonitorCount, $sysX + 10, $sysY + 95, $sysLabelW, $sysLabelH)
	$g_idSystemInfo = GUICtrlCreateLabel("System: " & _ImageSearch_GetSysInfo(), $sysX + 10, $sysY + 120, $sysLabelW, $sysInfoLabelH, $SS_LEFT)
	GUICtrlCreateGroup("", -99, -99, 1, 1)

	; Action buttons
	$g_idBtnSelectAll = GUICtrlCreateButton("Select All", $btnSelectAllX, $btnSelectAllY, $btnSelectAllW, $btnSelectAllH)
	$g_idBtnStart = GUICtrlCreateButton("Start Search", $btnStartX, $btnStartY, $btnStartW, $btnStartH, $BS_DEFPUSHBUTTON)
	GUICtrlSetFont(-1, 12, 800, 0, "Segoe UI")
	$g_idBtnDeselectAll = GUICtrlCreateButton("Deselect All", $btnDeselectAllX, $btnDeselectAllY, $btnDeselectAllW, $btnDeselectAllH)

	; Status bar
	$g_hStatusBar = _GUICtrlStatusBar_Create($g_hMainGUI)
	_UpdateStatus("Ready - ImageSearch Test Suite v3.3")

	GUISetState(@SW_SHOW)
	_UpdateSearchModeControls()

	; Log initial info
	_LogWrite("═══════════════════════════════════════════════════════════")
	_LogWrite("ImageSearch Test Suite v3.3 - Initialized Successfully")
	_LogWrite("UDF Version: " & $IMGS_UDF_VERSION & "  |  DLL: " & _ImageSearch_GetVersion())
	_LogWrite("Detected " & $g_iMonitorCount & " monitor(s)")
	If $g_iMonitorCount > 0 Then
		For $i = 1 To $g_iMonitorCount
			Local $sMonInfo = "  Monitor " & $i & ": " & $g_aMonitorList[$i][5] & "x" & $g_aMonitorList[$i][6]
			If $g_aMonitorList[$i][7] Then $sMonInfo &= " (Primary)"
			_LogWrite($sMonInfo)
		Next
	EndIf
	_LogWrite("═══════════════════════════════════════════════════════════")
	_LogWrite("Ready to search. Select images and click 'Start Search'")
EndFunc   ;==>_CreateGUI

Func _UpdateGUI_SystemInfo()
	GUICtrlSetData($g_idSystemInfo, "System Info: " & _ImageSearch_GetSysInfo())
EndFunc   ;==>_UpdateGUI_SystemInfo

; #FUNCTION# ====================================================================================================================
; Name...........: _GetControlPos
; Description....: Calculates the X and Y coordinates for a control within a grid layout based on its index.
; Parameters.....: $iIndex     - The zero-based index of the control in the grid.
;                  $iX_Start   - The starting X coordinate of the grid.
;                  $iY_Start   - The starting Y coordinate of the grid.
;                  $iColWidth  - The width of each column.
;                  $iRowHeight - The height of each row.
;                  $iX         - [ByRef] Returns the calculated X coordinate.
;                  $iY         - [ByRef] Returns the calculated Y coordinate.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _GetControlPos($iIndex, $iX_Start, $iY_Start, $iColWidth, $iRowHeight, ByRef $iX, ByRef $iY)
	$iX = $iX_Start + ($iColWidth * Mod($iIndex, 4)) ; 4 controls per row
	$iY = $iY_Start + ($iRowHeight * Int($iIndex / 4))
EndFunc   ;==>_GetControlPos

; #FUNCTION# ====================================================================================================================
; Name...........: _CreateCheckboxGrid
; Description....: Creates a grid of checkboxes for selecting which image slots to search.
; Parameters.....: $aidArray   - Array to store control IDs.
;                  $iCount     - Number of checkboxes to create.
;                  $iX_Start   - Starting X coordinate.
;                  $iY_Start   - Starting Y coordinate.
;                  $iColWidth  - Width of each column.
;                  $iRowHeight - Height of each row.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _CreateCheckboxGrid(ByRef $aidArray, $iCount, $iX_Start, $iY_Start, $iColWidth, $iRowHeight)
	For $i = 0 To $iCount - 1
		Local $iX, $iY
		_GetControlPos($i, $iX_Start, $iY_Start, $iColWidth, $iRowHeight, $iX, $iY)
		$aidArray[$i] = GUICtrlCreateCheckbox("Search " & ($i + 1), $iX, $iY, 100, 20)
	Next
EndFunc   ;==>_CreateCheckboxGrid

; #FUNCTION# ====================================================================================================================
; Name...........: _CreateBtnCreateGrid
; Description....: Creates a grid of "Create" buttons for capturing images from the screen.
; Parameters.....: $aidArray   - Array to store control IDs.
;                  $iCount     - Number of buttons to create.
;                  $iX_Start   - Starting X coordinate.
;                  $iY_Start   - Starting Y coordinate.
;                  $iColWidth  - Width of each column.
;                  $iRowHeight - Height of each row.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _CreateBtnCreateGrid(ByRef $aidArray, $iCount, $iX_Start, $iY_Start, $iColWidth, $iRowHeight)
	For $i = 0 To $iCount - 1
		Local $iX, $iY
		_GetControlPos($i, $iX_Start, $iY_Start, $iColWidth, $iRowHeight, $iX, $iY)
		$aidArray[$i] = GUICtrlCreateButton("Create", $iX, $iY + 125, 45, 20)
		GUICtrlSetTip(-1, "Capture an image from the screen for slot " & ($i + 1) & ".")
	Next
EndFunc   ;==>_CreateBtnCreateGrid

; #FUNCTION# ====================================================================================================================
; Name...........: _CreateBtnBrowseGrid
; Description....: Creates a grid of "Browse" buttons for selecting existing image files.
; Parameters.....: $aidArray   - Array to store control IDs.
;                  $iCount     - Number of buttons to create.
;                  $iX_Start   - Starting X coordinate.
;                  $iY_Start   - Starting Y coordinate.
;                  $iColWidth  - Width of each column.
;                  $iRowHeight - Height of each row.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _CreateBtnBrowseGrid(ByRef $aidArray, $iCount, $iX_Start, $iY_Start, $iColWidth, $iRowHeight)
	For $i = 0 To $iCount - 1
		Local $iX, $iY
		_GetControlPos($i, $iX_Start, $iY_Start, $iColWidth, $iRowHeight, $iX, $iY)
		$aidArray[$i] = GUICtrlCreateButton("Browse", $iX + 50, $iY + 125, 45, 20)
		GUICtrlSetTip(-1, "Select an existing image file for slot " & ($i + 1) & ".")
	Next
EndFunc   ;==>_CreateBtnBrowseGrid

; #FUNCTION# ====================================================================================================================
; Name...........: _CreatePicGrid
; Description....: Creates a grid of image preview controls to display the target images.
; Parameters.....: $aidArray   - Array to store control IDs.
;                  $iCount     - Number of controls to create.
;                  $iX_Start   - Starting X coordinate.
;                  $iY_Start   - Starting Y coordinate.
;                  $iColWidth  - Width of each column.
;                  $iRowHeight - Height of each row.
;                  $iPicWidth  - Width of each image preview.
;                  $iPicHeight - Height of each image preview.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _CreatePicGrid(ByRef $aidArray, $iCount, $iX_Start, $iY_Start, $iColWidth, $iRowHeight, $iPicWidth, $iPicHeight)
	For $i = 0 To $iCount - 1
		Local $iX, $iY
		_GetControlPos($i, $iX_Start, $iY_Start, $iColWidth, $iRowHeight, $iX, $iY)
		$aidArray[$i] = GUICtrlCreatePic("", $iX, $iY, $iPicWidth, $iPicHeight)
		GUICtrlSetState(-1, $GUI_DISABLE)
	Next
EndFunc   ;==>_CreatePicGrid

; #FUNCTION# ====================================================================================================================
; Name...........: _UpdateSearchModeControls
; Description....: Updates the state (enabled/disabled) of GUI controls based on the selected search mode (screen or image).
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _UpdateSearchModeControls()
	Local $bIsScreenSearch = __IsChecked($g_idRadSearchOnScreen)
	GUICtrlSetState($g_idInputSourceImage, ($bIsScreenSearch ? $GUI_DISABLE : $GUI_ENABLE))
	GUICtrlSetState($g_idBtnBrowseSource, ($bIsScreenSearch ? $GUI_DISABLE : $GUI_ENABLE))
	GUICtrlSetState($g_idChkMoveMouse, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idRadNoClick, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idRadSingleClick, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idRadDoubleClick, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idChkUseArea, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idInputLeft, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idInputTop, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idInputRight, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idInputBottom, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idBtnSelectArea, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idChkWait, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	GUICtrlSetState($g_idInputWaitTime, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))

	; NEW v3.3: Monitor selection controls
	GUICtrlSetState($g_idChkUseSpecificMonitor, ($bIsScreenSearch ? $GUI_ENABLE : $GUI_DISABLE))
	If Not $bIsScreenSearch Then
		GUICtrlSetState($g_idComboMonitor, $GUI_DISABLE)
	Else
		_UpdateMonitorControls()
	EndIf
EndFunc   ;==>_UpdateSearchModeControls

; #FUNCTION# ====================================================================================================================
; Name...........: _UpdateMonitorControls
; Description....: Updates monitor selection controls based on checkbox state
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _UpdateMonitorControls()
	Local $bScreenSearch = __IsChecked($g_idRadSearchOnScreen)
	Local $bUseSpecific = __IsChecked($g_idChkUseSpecificMonitor)

	If $bScreenSearch And $bUseSpecific Then
		GUICtrlSetState($g_idComboMonitor, $GUI_ENABLE)
	Else
		GUICtrlSetState($g_idComboMonitor, $GUI_DISABLE)
	EndIf
EndFunc   ;==>_UpdateMonitorControls

; #REGION# === EVENT HANDLERS ===

; #FUNCTION# ====================================================================================================================
; Name...........: _HandleImageCreation
; Description....: Handles the "Create" button click for an image slot by capturing a screen region and saving it to a file.
; Parameters.....: $iControlID - The control ID of the clicked "Create" button.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _HandleImageCreation($iControlID)
	For $i = 0 To $MAX_IMAGES - 1
		If $g_aidBtnCreate[$i] = $iControlID Then
			GUISetState(@SW_HIDE, $g_hMainGUI)
			_UpdateStatus("Capturing image for slot " & ($i + 1) & "...")
			Local $iResult = _CaptureRegion_free($g_asImagePaths[$i])
			GUISetState(@SW_SHOW, $g_hMainGUI)
			If $iResult = 0 Then
				_LogWrite("Captured image for slot " & ($i + 1) & ": " & $g_asImagePaths[$i])
				_UpdateSingleImagePreview($i)
				_RefreshImageTooltips()
				_UpdateStatus("Image captured for slot " & ($i + 1))
			ElseIf $iResult = -2 Then
				_LogWrite("Image capture cancelled for slot " & ($i + 1))
				_UpdateStatus("Capture cancelled")
			Else
				_LogWrite("ERROR: Failed to capture image for slot " & ($i + 1))
				_UpdateStatus("Capture failed")
			EndIf
			ExitLoop
		EndIf
	Next
EndFunc   ;==>_HandleImageCreation

; #FUNCTION# ====================================================================================================================
; Name...........: _HandleImageBrowse
; Description....: Handles the "Browse" button click for an image slot by opening a file dialog and copying the selected image.
; Parameters.....: $iControlID - The control ID of the clicked "Browse" button.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _HandleImageBrowse($iControlID)
	For $i = 0 To $MAX_IMAGES - 1
		If $g_aidBtnBrowse[$i] = $iControlID Then
			Local $sFile = FileOpenDialog("Select Image for Slot " & ($i + 1), @ScriptDir, "Images (*.bmp;*.jpg;*.jpeg;*.png;*.gif)", 1)
			If @error Or Not _ValidateImageFile($sFile) Then
				_LogWrite("ERROR: Invalid or no image selected for slot " & ($i + 1))
				_UpdateStatus("Invalid image selected")
				Return
			EndIf
			If FileExists($g_asImagePaths[$i]) Then FileDelete($g_asImagePaths[$i])
			If FileCopy($sFile, $g_asImagePaths[$i], 1) Then
				_LogWrite("Selected image for slot " & ($i + 1) & ": " & $g_asImagePaths[$i])
				_UpdateSingleImagePreview($i)
				_RefreshImageTooltips()
				_UpdateStatus("Image selected for slot " & ($i + 1))
			Else
				_LogWrite("ERROR: Failed to copy image for slot " & ($i + 1))
				_UpdateStatus("Failed to copy image")
			EndIf
			ExitLoop
		EndIf
	Next
EndFunc   ;==>_HandleImageBrowse

; #FUNCTION# ====================================================================================================================
; Name...........: _BrowseSourceImage
; Description....: Opens a file dialog to select the source image for image-in-image search and updates the GUI.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _BrowseSourceImage()
	Local $sFile = FileOpenDialog("Select Source Image", @ScriptDir, "Images (*.bmp;*.jpg;*.jpeg;*.png;*.gif)", 1)
	If @error Or Not _ValidateImageFile($sFile) Then
		_LogWrite("ERROR: Invalid or no source image selected.")
		_UpdateStatus("Invalid source image")
		Return
	EndIf
	GUICtrlSetData($g_idInputSourceImage, $sFile)
	_LogWrite("Selected source image: " & $sFile)
	_UpdateStatus("Source image selected")
EndFunc   ;==>_BrowseSourceImage

; #FUNCTION# ====================================================================================================================
; Name...........: _ExecuteSearch
; Description....: Executes the image search based on user-specified parameters from the GUI and processes the results.
; Parameters.....: None (uses global GUI control states)
; Return values..: True if search executed successfully, False otherwise.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _ExecuteSearch()
	_UpdateGUI_SystemInfo()
	; Get search parameters from GUI controls
	Local $bMultiSearch = __IsChecked($g_idChkMultiSearch)
	Local $bFindAll = __IsChecked($g_idChkFindAll)
	Local $bWait = __IsChecked($g_idChkWait)
	Local $bUseTolerance = __IsChecked($g_idChkUseTolerance)
	Local $bEnableDebug = __IsChecked($g_idChkEnableDebug)
	Local $bUseCache = __IsChecked($g_idChkUseCache)
	Local $bUseArea = __IsChecked($g_idChkUseArea)
	Local $bMoveMouse = __IsChecked($g_idChkMoveMouse)
	Local $bSearchInImage = __IsChecked($g_idRadSearchInImage)
	Local $iTimeout = Number(GUICtrlRead($g_idInputWaitTime))
	Local $iTolerance = ($bUseTolerance ? Number(GUICtrlRead($g_idInputTolerance)) : 0)
	Local $iDelay = Number(GUICtrlRead($g_idInputDelay))
	Local $iLeft = ($bUseArea ? Number(GUICtrlRead($g_idInputLeft)) : 0)
	Local $iTop = ($bUseArea ? Number(GUICtrlRead($g_idInputTop)) : 0)
	Local $iRight = ($bUseArea ? Number(GUICtrlRead($g_idInputRight)) : 0)
	Local $iBottom = ($bUseArea ? Number(GUICtrlRead($g_idInputBottom)) : 0)

	; NEW v3.3: Monitor selection
	Local $iScreen = -1  ; Default: all monitors (virtual desktop)
	Local $bUseSpecificMonitor = __IsChecked($g_idChkUseSpecificMonitor)
	If $bUseSpecificMonitor And Not $bSearchInImage Then
		; Parse monitor number from combobox selection
		Local $sMonitorText = GUICtrlRead($g_idComboMonitor)
		Local $aMatch = StringRegExp($sMonitorText, "Monitor (\d+)", 1)
		If Not @error And IsArray($aMatch) Then
			$iScreen = Number($aMatch[0])
			_LogWrite("Using specific monitor: " & $iScreen & " (faster search)")
		EndIf
	EndIf

	; Cache parameter (v3.3 uses $IMGS_ENABLED_CACHE constant from UDF)
	Local $iUseCache = $bUseCache ? $IMGS_ENABLED_CACHE : 0
	If $bUseCache Then _LogWrite("Cache enabled for performance boost")

	Local $iCenterPos = 1 ; Return center coordinates
	Local $fMinScale = 1.0, $fMaxScale = 1.0, $fScaleStep = 0.1
	Local $iReturnDebug = ($bEnableDebug ? 1 : 0)
	Local $sClickType = (__IsChecked($g_idRadSingleClick) ? "single" : (__IsChecked($g_idRadDoubleClick) ? "double" : "none"))
	Local $iClicks = ($sClickType = "single" ? 1 : ($sClickType = "double" ? 2 : 0))

	; Build list of images to search with detailed logging
	Local $sImagePath = ""
	Local $sValidImages = ""
	Local $sInvalidImages = ""
	_LogWrite("Building list of images to search...")
	For $i = 0 To $MAX_IMAGES - 1
		If __IsChecked($g_aidChkSearch[$i]) Then
			If FileExists($g_asImagePaths[$i]) Then
				$sImagePath &= ($sImagePath = "" ? "" : "|") & $g_asImagePaths[$i]
				$sValidImages &= ($sValidImages = "" ? "" : ", ") & "Slot " & ($i + 1)
			Else
				GUICtrlSetState($g_aidChkSearch[$i], $GUI_UNCHECKED) ; Uncheck if image does not exist
				$sInvalidImages &= ($sInvalidImages = "" ? "" : ", ") & "Slot " & ($i + 1)
			EndIf
		EndIf
	Next
	If $sValidImages <> "" Then
		_LogWrite("Valid images selected: " & $sValidImages)
	Else
		_LogWrite("No valid images found.")
	EndIf
	If $sInvalidImages <> "" Then
		_LogWrite("Invalid or missing images (unchecked): " & $sInvalidImages)
	EndIf

	If $sImagePath = "" Then
		_LogWrite("ERROR: No valid images selected for search.")
		_UpdateStatus("Error: No images selected")
		Return False
	EndIf

	; Initialize result array
	Local $aResult
	Local $bSuccess = False

	If $bSearchInImage Then
		; Search within a source image
		Local $sSourceImage = GUICtrlRead($g_idInputSourceImage)
		If Not FileExists($sSourceImage) Then
			_LogWrite("ERROR: Source image file does not exist: " & $sSourceImage)
			_UpdateStatus("Error: Invalid source image")
			Return False
		EndIf

		_LogWrite("Starting image-in-image search for: " & $sImagePath)
		_UpdateStatus("Searching in image...")

		; Execute search
		$aResult = _ImageSearch_InImage($sSourceImage, $sImagePath, $iTolerance, ($bFindAll ? $MAX_IMAGES : 1), $iCenterPos, $fMinScale, $fMaxScale, $fScaleStep, $iReturnDebug, $iUseCache)
		If $bEnableDebug Then
			_LogWrite("DLL Raw Return: " & _ImageSearch_GetLastResult())
		EndIf
		If @error Then
			_LogSearchError(@error)
			_UpdateStatus("Search failed")
			Return False
		EndIf

		; Process results (v3.3: array format is [row][col] where col: 0=X, 1=Y, 2=W, 3=H)
		If $aResult[0][0] > 0 Then
			For $i = 1 To $aResult[0][0]
				_LogWrite("Found match " & $i & ": X=" & $aResult[$i][0] & ", Y=" & $aResult[$i][1] & ", Width=" & $aResult[$i][2] & ", Height=" & $aResult[$i][3])
				$bSuccess = True
			Next
			_UpdateStatus("Found " & $aResult[0][0] & " match(es)")
		Else
			_LogWrite("No matches found.")
			_UpdateStatus("No matches found")
		EndIf
	Else
		; Screen search
		_LogWrite("Starting screen search for: " & $sImagePath)
		_UpdateStatus("Searching on screen...")

		; v3.3: All searches now use _ImageSearch with proper parameters
		If $bWait Then
			; Wait for image to appear (same params for area or full screen)
			$aResult = _ImageSearch_Wait($iTimeout, $sImagePath, $iLeft, $iTop, $iRight, $iBottom, $iScreen, $iTolerance, ($bFindAll ? $MAX_IMAGES : 1), $iCenterPos, $fMinScale, $fMaxScale, $fScaleStep, $iReturnDebug, $iUseCache)
		Else
			; Immediate search (replaced _ImageSearch_Area with _ImageSearch)
			$aResult = _ImageSearch($sImagePath, $iLeft, $iTop, $iRight, $iBottom, $iScreen, $iTolerance, ($bFindAll ? $MAX_IMAGES : 1), $iCenterPos, $fMinScale, $fMaxScale, $fScaleStep, $iReturnDebug, $iUseCache)
		EndIf

		If $bEnableDebug Then
			_LogWrite("DLL Raw Return: " & _ImageSearch_GetLastResult())
		EndIf
		If @error Then
			_LogSearchError(@error)
			_UpdateStatus("Search failed")
			Return False
		EndIf

		; Process results (v3.3: array format is [row][col] where col: 0=X, 1=Y, 2=W, 3=H)
		If $aResult[0][0] > 0 Then
			For $i = 1 To $aResult[0][0]
				_LogWrite("Found match " & $i & ": X=" & $aResult[$i][0] & ", Y=" & $aResult[$i][1] & ", Width=" & $aResult[$i][2] & ", Height=" & $aResult[$i][3])
				If $bMoveMouse Then
					_UpdateStatus("MouseMove X:"&$aResult[$i][0] &" Y:"& $aResult[$i][1])
					; Use DLL MouseMove to support negative coordinates
					_ImageSearch_MouseMove($aResult[$i][0], $aResult[$i][1], 0, $iScreen)
					_HighlightFoundArea($aResult[$i][0], $aResult[$i][1], $aResult[$i][2], $aResult[$i][3])
				EndIf
				If $iClicks > 0 Then
					_UpdateStatus("MouseClick 'left' X:"&$aResult[$i][0] &" Y:"& $aResult[$i][1])
					; Use DLL MouseClick with fallback support for negative coordinates
					_ImageSearch_MouseClick("left", $aResult[$i][0], $aResult[$i][1], $iClicks, 0, $iScreen)
				EndIf
				Sleep($iDelay)
				$bSuccess = True
			Next
			_UpdateStatus("Found " & $aResult[0][0] & " match(es)")
		Else
			_LogWrite("No matches found.")
			_UpdateStatus("No matches found")
		EndIf
	EndIf

	Return $bSuccess
EndFunc   ;==>_ExecuteSearch


; #FUNCTION# ====================================================================================================================
; Name...........: _SelectAreaOnScreen
; Description....: Allows the user to select a rectangular area on the screen by dragging the mouse and updates the GUI inputs.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _SelectAreaOnScreen()
	GUISetState(@SW_HIDE, $g_hMainGUI)
	_UpdateStatus("Selecting search area...")
	Local $aArea = _CaptureRegion_free()
	GUISetState(@SW_SHOW, $g_hMainGUI)
	If IsArray($aArea) Then
		GUICtrlSetData($g_idInputLeft, $aArea[0])
		GUICtrlSetData($g_idInputTop, $aArea[1])
		GUICtrlSetData($g_idInputRight, $aArea[2])
		GUICtrlSetData($g_idInputBottom, $aArea[3])
		GUICtrlSetState($g_idChkUseArea, $GUI_CHECKED)
		_LogWrite("Search area selected: Left=" & $aArea[0] & ", Top=" & $aArea[1] & ", Right=" & $aArea[2] & ", Bottom=" & $aArea[3])
		_UpdateStatus("Search area selected")
	ElseIf $aArea = -2 Then
		_LogWrite("Area selection cancelled.")
		_UpdateStatus("Area selection cancelled")
	Else
		_LogWrite("ERROR: Failed to select search area.")
		_UpdateStatus("Area selection failed")
	EndIf
EndFunc   ;==>_SelectAreaOnScreen

; #REGION# === SCREEN CAPTURE & AREA SELECTION ===

; #FUNCTION# ====================================================================================================================
; Name...........: _CaptureRegion_free
; Description....: Captures a user-defined rectangular region of the screen or returns the coordinates of the selected area.
; Parameters.....: $sFilePath - [optional] The file path to save the captured image. If empty, returns coordinates.
; Return values..: If $sFilePath is provided:
;                  | 0 = Success (image saved)
;                  | -1 = Capture failed
;                  | -2 = User cancelled
;                  If $sFilePath is empty:
;                  | Array [Left, Top, Right, Bottom] = Success
;                  | -1 = Capture failed
;                  | -2 = User cancelled
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _CaptureRegion_free($sFilePath = "")
	Local $sTitle = "Select Region (" & ($sFilePath = "" ? "Area" : "Image") & ")"
	Local $hUserDLL = DllOpen("user32.dll")
	If $hUserDLL = -1 Then Return -1

	; Create a fullscreen, transparent window to capture mouse events.
	Local $hCrossGUI = GUICreate($sTitle, @DesktopWidth, @DesktopHeight, 0, 0, $WS_POPUP, $WS_EX_TOPMOST)
	GUISetBkColor(0x000001) ; A color that is unlikely to be on screen.
	WinSetTrans($hCrossGUI, "", 1) ; Make it almost fully transparent.
	GUISetState(@SW_SHOW, $hCrossGUI)
	GUISetCursor(3, 1, $hCrossGUI) ; Set crosshair cursor.

	_UpdateStatus("Drag the mouse to select an area. Press ESC to cancel.")
	ToolTip("Drag the mouse to select an area. Press ESC to cancel.", 0, 0)

	; Wait for the user to press the left mouse button.
	While Not _IsPressed("01", $hUserDLL)
		If _IsPressed("1B", $hUserDLL) Then ; Check for ESC key press to cancel.
			ToolTip("")
			GUIDelete($hCrossGUI)
			DllClose($hUserDLL)
			GUISetState(@SW_SHOW, $g_hMainGUI)
			Return -2
		EndIf
		Sleep(20)
	WEnd
	ToolTip("")

	Local $aStartPos = MouseGetPos()
	Local $iX1 = $aStartPos[0], $iY1 = $aStartPos[1]
	Local $hRectGUI

	; While the mouse button is held down, draw a feedback rectangle.
	While _IsPressed("01", $hUserDLL)
		Local $aCurrentPos = MouseGetPos()
		Local $iX2 = $aCurrentPos[0], $iY2 = $aCurrentPos[1]
		If IsHWnd($hRectGUI) Then GUIDelete($hRectGUI)

		Local $iLeft = ($iX1 < $iX2 ? $iX1 : $iX2)
		Local $iTop = ($iY1 < $iY2 ? $iY1 : $iY2)
		Local $iWidth = Abs($iX1 - $iX2)
		Local $iHeight = Abs($iY1 - $iY2)

		$hRectGUI = GUICreate("", $iWidth, $iHeight, $iLeft, $iTop, $WS_POPUP, BitOR($WS_EX_LAYERED, $WS_EX_TOPMOST))
		GUISetBkColor(0xFF0000) ; Red feedback rectangle.
		_WinAPI_SetLayeredWindowAttributes($hRectGUI, 0, 100) ; Set transparency.
		GUISetState(@SW_SHOWNOACTIVATE, $hRectGUI)
		Sleep(10)
	WEnd

	Local $aEndPos = MouseGetPos()
	Local $iX2 = $aEndPos[0], $iY2 = $aEndPos[1]

	; Clean up the temporary GUIs.
	GUIDelete($hCrossGUI)
	If IsHWnd($hRectGUI) Then GUIDelete($hRectGUI)
	DllClose($hUserDLL)

	; Final coordinate calculation.
	Local $iLeft = ($iX1 < $iX2 ? $iX1 : $iX2)
	Local $iTop = ($iY1 < $iY2 ? $iY1 : $iY2)
	Local $iRight = ($iX1 > $iX2 ? $iX1 : $iX2)
	Local $iBottom = ($iY1 > $iY2 ? $iY1 : $iY2)

	GUISetState(@SW_SHOW, $g_hMainGUI)

	; If no area was selected (no drag), treat as a cancel.
	If $iLeft = $iRight Or $iTop = $iBottom Then Return -2

	; If a file path was provided, capture the screen area to that file.
	If $sFilePath <> "" Then
		Local $aMousePos = MouseGetPos()
		MouseMove(0, 0, 0) ; Move mouse out of the way for a clean capture.
		Sleep(250)
		Local $hBitmap = _ScreenCapture_Capture("", $iLeft, $iTop, $iRight, $iBottom, False)
		If @error Then
			MouseMove($aMousePos[0], $aMousePos[1], 0)
			Return -1 ; Return error if capture failed.
		EndIf
		Local $hImage = _GDIPlus_BitmapCreateFromHBITMAP($hBitmap)
		_GDIPlus_ImageSaveToFile($hImage, $sFilePath)
		_GDIPlus_BitmapDispose($hImage)
		_WinAPI_DeleteObject($hBitmap)
		MouseMove($aMousePos[0], $aMousePos[1], 0) ; Restore mouse position.
		Return 0 ; Success.
	Else
		; If no file path, return the coordinates array.
		Local $aReturn[4] = [$iLeft, $iTop, $iRight, $iBottom]
		Return $aReturn
	EndIf
EndFunc   ;==>_CaptureRegion_free

; === END SCREEN CAPTURE & AREA SELECTION ===

; #REGION# === UTILITY & HELPER FUNCTIONS ===

; #FUNCTION# ====================================================================================================================
; Name...........: _HighlightFoundArea
; Description....: Creates a temporary, semi-transparent GUI to visually highlight a found image location on the screen.
; Parameters.....: $iX, $iY    - The TOP-LEFT coordinates of the found area (NOT center).
;                  $iWidth     - The width of the highlight rectangle.
;                  $iHeight    - The height of the highlight rectangle.
;                  $iColor     - [optional] The color of the highlight rectangle in 0xRRGGBB format. Default is green.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _HighlightFoundArea($iX, $iY, $iWidth, $iHeight, $iColor = 0xFF00FF00)
	; FIX: $iX/$iY are already top-left coordinates, not center
	Local $hGUI = GUICreate("", $iWidth, $iHeight, $iX, $iY, $WS_POPUP, BitOR($WS_EX_LAYERED, $WS_EX_TOPMOST, $WS_EX_TOOLWINDOW))
	GUISetBkColor($iColor)
	_WinAPI_SetLayeredWindowAttributes($hGUI, 0, 128) ; 50% transparency.
	GUISetState(@SW_SHOWNOACTIVATE)
	Sleep(500) ; Display the highlight for half a second.
	GUIDelete($hGUI)
EndFunc   ;==>_HighlightFoundArea

; #FUNCTION# ====================================================================================================================
; Name...........: _LogWrite
; Description....: Writes a timestamped message to the activity log Edit control and ensures it scrolls to the latest entry.
; Parameters.....: $sMessage - The string message to log.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _LogWrite($sMessage)
	GUICtrlSetData($g_hLog, "[" & _NowTime(5) & "] " & $sMessage & @CRLF, 1) ; The '1' appends the text.
	_GUICtrlEdit_SetSel(GUICtrlGetHandle($g_hLog), 0x7FFFFFFF, 0x7FFFFFFF) ; Scroll to the end.
	ConsoleWrite($sMessage&@CRLF)
EndFunc   ;==>_LogWrite

; #FUNCTION# ====================================================================================================================
; Name...........: _LogSearchError
; Description....: Translates a numerical error code from the ImageSearch UDF into a human-readable message and logs it.
; Parameters.....: $iErrorCode - The status code returned by an _ImageSearch* function.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _LogSearchError($iErrorCode)
	Switch $iErrorCode
		Case 0
			_LogWrite("No matches found.")
		Case $IMGSE_FAILED_TO_GET_SCREEN_DC
			_LogWrite("ERROR: Failed to capture screen.")
		Case $IMGSE_INVALID_PARAMETERS
			_LogWrite("ERROR: Invalid parameters.")
		Case $IMGSE_INVALID_SEARCH_REGION
			_LogWrite("ERROR: Invalid search region.")
		Case $IMGSE_INVALID_Source_BITMAP
			_LogWrite("ERROR: Invalid Source bitmap.")
		Case $IMGSE_RESULT_TOO_LARGE
			_LogWrite("ERROR: Too many results.")
		Case Else
			_LogWrite("ERROR: Error code " & $iErrorCode)
	EndSwitch
EndFunc   ;==>_LogSearchError

; #FUNCTION# ====================================================================================================================
; Name...........: _UpdateAllImagePreviews
; Description....: Iterates through all image slots and calls the function to update their preview images.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _UpdateAllImagePreviews()
	For $i = 0 To $MAX_IMAGES - 1
		_UpdateSingleImagePreview($i)
	Next
EndFunc   ;==>_UpdateAllImagePreviews

; #FUNCTION# ====================================================================================================================
; Name...........: _UpdateSingleImagePreview
; Description....: Updates a single image preview slot. If the target image file exists, it's displayed. Otherwise, a placeholder is shown.
; Parameters.....: $iIndex - The index (0-11) of the image slot to update.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _UpdateSingleImagePreview($iIndex)
	If FileExists($g_asImagePaths[$iIndex]) Then
		GUICtrlSetImage($g_aidPic[$iIndex], $g_asImagePaths[$iIndex])
	Else
		; Use a default Windows wallpaper as a placeholder if available.
		If FileExists($g_sPlaceholderPath) Then
			GUICtrlSetImage($g_aidPic[$iIndex], $g_sPlaceholderPath)
		Else
			; Fallback to a generic icon from shell32.dll if the wallpaper is also missing.
			GUICtrlSetImage($g_aidPic[$iIndex], "shell32.dll", 22)
		EndIf
	EndIf
EndFunc   ;==>_UpdateSingleImagePreview

; #FUNCTION# ====================================================================================================================
; Name...........: _SelectAll
; Description....: Checks or unchecks all image target checkboxes simultaneously.
; Parameters.....: $bState - True to check all, False to uncheck all.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _SelectAll($bState)
	Local $iCheckState = ($bState ? $GUI_CHECKED : $GUI_UNCHECKED)

	For $i = 0 To $MAX_IMAGES - 1
		GUICtrlSetState($g_aidChkSearch[$i], $iCheckState)
	Next
EndFunc   ;==>_SelectAll

; #FUNCTION# ====================================================================================================================
; Name...........: _UpdateStatus
; Description....: Sets the text of the status bar at the bottom of the GUI.
; Parameters.....: $sMessage - The message to display.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _UpdateStatus($sMessage)
	_GUICtrlStatusBar_SetText($g_hStatusBar, $sMessage)
EndFunc   ;==>_UpdateStatus

; #FUNCTION# ====================================================================================================================
; Name...........: __IsChecked
; Description....: A helper function to check the state of a checkbox or radio button in a more readable way.
; Parameters.....: $iControlID - The control ID of the checkbox or radio button.
; Return values..: True if the control is checked, False otherwise.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func __IsChecked($iControlID)
	Return BitAND(GUICtrlRead($iControlID), $GUI_CHECKED) = $GUI_CHECKED
EndFunc   ;==>__IsChecked

; #FUNCTION# ====================================================================================================================
; Name...........: _GetImageDimensions
; Description....: Gets the width and height of an image file using GDI+.
; Parameters.....: $sImagePath - Path to the image file.
; Return values..: On success, a 2-element array [Width, Height]. On failure, returns False.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _GetImageDimensions($sImagePath)
	If Not FileExists($sImagePath) Then Return False

	Local $hImage = _GDIPlus_ImageLoadFromFile($sImagePath)
	If Not $hImage Then Return False

	Local $iWidth = _GDIPlus_ImageGetWidth($hImage)
	Local $iHeight = _GDIPlus_ImageGetHeight($hImage)
	_GDIPlus_ImageDispose($hImage)

	Local $aDimensions[2] = [$iWidth, $iHeight]
	Return $aDimensions
EndFunc   ;==>_GetImageDimensions

; #FUNCTION# ====================================================================================================================
; Name...........: _ValidateImageFile
; Description....: Validates if a file is a valid image that can be processed by GDI+. Checks for existence, size, and basic integrity.
; Parameters.....: $sImagePath - Path to the image file.
; Return values..: True if the image is valid, False otherwise.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _ValidateImageFile($sImagePath)
	If Not FileExists($sImagePath) Then Return False

	; Check file size (must be > 0 and < 50MB for reasonable processing)
	Local $iFileSize = FileGetSize($sImagePath)
	If $iFileSize <= 0 Or $iFileSize > 50 * 1024 * 1024 Then Return False

	; Try to load with GDI+ to validate it's a real, uncorrupted image.
	Local $hImage = _GDIPlus_ImageLoadFromFile($sImagePath)
	If Not $hImage Then Return False

	; Check minimum dimensions (at least 1x1 pixel).
	Local $iWidth = _GDIPlus_ImageGetWidth($hImage)
	Local $iHeight = _GDIPlus_ImageGetHeight($hImage)
	_GDIPlus_ImageDispose($hImage)

	Return ($iWidth > 0 And $iHeight > 0)
EndFunc   ;==>_ValidateImageFile

; #FUNCTION# ====================================================================================================================
; Name...........: _CreateImageInfoTooltip
; Description....: Creates a detailed tooltip string for an image slot, showing file name, dimensions, file size, and full path.
; Parameters.....: $iIndex - The index (0-11) of the image slot.
; Return values..: A formatted string containing the image's information for use in a tooltip.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _CreateImageInfoTooltip($iIndex)
	Local $sImagePath = $g_asImagePaths[$iIndex]
	Local $sTooltip = "Slot " & ($iIndex + 1) & ":" & @CRLF

	If FileExists($sImagePath) Then
		Local $aDims = _GetImageDimensions($sImagePath)
		Local $iFileSize = FileGetSize($sImagePath)
		Local $sFileSize = ""

		If $iFileSize < 1024 Then
			$sFileSize = $iFileSize & " B"
		ElseIf $iFileSize < 1024 * 1024 Then
			$sFileSize = Round($iFileSize / 1024, 1) & " KB"
		Else
			$sFileSize = Round($iFileSize / (1024 * 1024), 2) & " MB"
		EndIf

		$sTooltip &= "File: " & StringRegExpReplace($sImagePath, ".*\\", "") & @CRLF
		If IsArray($aDims) Then
			$sTooltip &= "Size: " & $aDims[0] & " x " & $aDims[1] & " px" & @CRLF
		EndIf
		$sTooltip &= "File Size: " & $sFileSize & @CRLF
		$sTooltip &= "Path: " & $sImagePath
	Else
		$sTooltip &= "No image file." & @CRLF
		$sTooltip &= "Click 'Create' or 'Browse'."
	EndIf

	Return $sTooltip
EndFunc   ;==>_CreateImageInfoTooltip

; #FUNCTION# ====================================================================================================================
; Name...........: _RefreshImageTooltips
; Description....: Updates the tooltips for all image preview controls with the latest file information.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _RefreshImageTooltips()
	For $i = 0 To $MAX_IMAGES - 1
		GUICtrlSetTip($g_aidPic[$i], _CreateImageInfoTooltip($i))
	Next
EndFunc   ;==>_RefreshImageTooltips

; #FUNCTION# ====================================================================================================================
; Name...........: _Exit
; Description....: Performs cleanup operations (like shutting down GDI+) and exits the script cleanly.
; Author.........: Dao Van Trong (TRONG.PRO)
; ===============================================================================================================================
Func _Exit()
	_GDIPlus_Shutdown()
	_ImageSearch_Shutdown()
	Exit
EndFunc   ;==>_Exit


Func _ImgEmptySlots()
	; This function holds the hex data for default_img.jpg
	; File size: 8.2 KB
	; Architecture: Not PE
	; Generated by AutoIt Embedded File Generator
	Local $sHexData = '0xFFD8FFE000104A46494600010101006000600000FFE1005A4578696600004D4D002A00000008000503010005000000010000004A03030001000000010000000051100001000000010100000051110004000000010000000051120004000000010000000000000000000186A00000B18FFFDB0043000201010201010202020202020202030503030303030604040305070607070706070708090B0908080A0807070A0D0A0A0B0C0C0C0C07090E0F0D0C0E0B0C0C0CFFDB004301020202030303060303060C0807080C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0C0CFFC00011080156015603012200021101031101FFC4001F0000010501010101010100000000000000000102030405060708090A0BFFC400B5100002010303020403050504040000017D01020300041105122131410613516107227114328191A1082342B1C11552D1F02433627282090A161718191A25262728292A3435363738393A434445464748494A535455565758595A636465666768696A737475767778797A838485868788898A92939495969798999AA2A3A4A5A6A7A8A9AAB2B3B4B5B6B7B8B9BAC2C3C4C5C6C7C8C9CAD2D3D4D5D6D7D8D9DAE1E2E3E4E5E6E7E8E9EAF1F2F3F4F5F6F7F8F9FAFFC4001F01000301010101010101'
	$sHexData &= '01010000000000000102030405060708090A0BFFC400B51100020102040403040705040400010277000102031104052131061241510761711322328108144291A1B1C109233352F0156272D10A162434E125F11718191A262728292A35363738393A434445464748494A535455565758595A636465666768696A737475767778797A82838485868788898A92939495969798999AA2A3A4A5A6A7A8A9AAB2B3B4B5B6B7B8B9BAC2C3C4C5C6C7C8C9CAD2D3D4D5D6D7D8D9DAE2E3E4E5E6E7E8E9EAF2F3F4F5F6F7F8F9FAFFDA000C03010002110311003F00F90E8A28AFED03F2F0A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A28'
	$sHexData &= '00A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A28'
	$sHexData &= '00A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A2A4B3B39B50BB8ADEDE29279E7711C71C6A59E462701401C924F000AFB3A3FD927E0F7EC23E14D3752FDA16E355F167C44D4A25BA83E1DE8178B10D3E221597FB42E14E632C0FDD4607FBBBC648F3730CD68E1396124E5397C318ABC9DB7B2D124BAB6D25D5EA7450C34AADDAD12DDBD97F5D96A7C5B457D8337FC14DBE1DD84A61D27F655F82D069AC70D1DFDA9BEB9DBD809CA2907DF6D68E85E2EFD96FF6D8BB1A2EAFE1597F676F195E46D1D8EB7A7DFF00DA7C38F2F3B05D4726DF254F4CA851DDA415E74B3BC5525ED313849C61D5A709B5E6E31937FF0080A91B2C25397BB4EAA6FE6BF16ADF7D8F8B68AF40FDA67F665F167EC95F15EEFC1FE31B38EDF508116E2DEE2DDCCB69A95B3E7CBB9B793037C4D8383804156560ACACA3CFEBDEA15E9D7A71AD464A5192BA6B668E39C250938C959A0A2BF487FE0DE3F857E1AF88FAF7C5F7F10681A46B6D6BA6585BC3F6EB44B8114731BAF355438380DB1338EBB457E7BFC45D3E1D23E20EBB696D1AC56F6BA8DC45120E888B230007D0015E4E0F3B862331C465CA2D3A2A177D1F3ABFE074D5C238508576FE3BFE0CC6A2B574CF02EB7AD786EF759B3D1B55BBD234C60B797D0DA4925B5A93D049201B509ED922B7FC21FB36FC45F883E1FF00ED6D03C03E35D734AC13F6'
	$sHexData &= 'DD3F43B9B9B7C0EBFBC442BFAD7AB53134609B9C92B69AB5BF639E34E4F448E2E8A92EED25B0BA9609E29219E1731C91C8A55E360705483C820F635D4F82BE01F8EBE24E95F6FF000E782BC5BAFD88629F68D3748B8BA8B70E08DD1A119FC6AAA56A74E3CF52492EEDD8518CA4ED157392A2AEF88BC35A97843579B4FD5B4FBDD2EFEDCE25B6BB81A09A33E8C8C011F88AAD676736A37715BDBC524F3CEE238E38D4B3C8C4E028039249E00154A49AE64F4134EF623A2B7D7E1478A5FC69FF0008D8F0D6BE7C459C7F658D3E6FB6E719FF0053B77F4E7A74AB9E3DF80FE38F855671DC78A3C19E2BF0DDBCA42A4BAA69171668E4F2003222826B3FACD1E651E7577B2BAD7D0AF672B376D8E528A29F6D6D25E5C470C31BCB2CAC111114B33B1380001D4935B10328AEDF55FD997E24685A23EA77DF0FBC6F67A7449E63DD4FA15D47022E33B8B940A063BE6B88ACA957A7515E9C93B76772A50947E2560A28A2B52428ADAF047C36F117C4CD45ECFC37A06B5E20BB8977BC1A6D8CB77222FA958D4902A5F1D7C2AF147C2FB88A1F13786F5FF0ECB38CC49A9E9F35A3483D5448A33F8565EDE9F3FB3E65CDDAFAFDC5724ADCD6D0C0A28AD7D4FC01AF689A3E9BA8DEE89ABDA69FAC8CD85CCD672470DF76FDD39187FF0080935729C5349BDC4937B1914576FAF7ECCFF11FC2DE156D7753F87FE3'
	$sHexData &= '7D3B4444F31B50BAD0AEA1B555FEF19590263DF35C4A234AE154166638000C926A2957A751374E49A5D9DC7284A3A49584A2BB997F661F897068A7527F879E394D3847E69BA6D06E84213FBDBFCBDB8F7CD70D452AF4EA5FD9C93B7677094251F895828A28AD490A28A2800A28A2800A28A2800A28A2800A28A2800A28A2803EB2FF008269693A77C1FF0008FC50F8FDAD58DADF2FC2BD2E383C3B0DCA878E4D72EDFCBB57DBC93E572C78E37060415C8F1EF8437FE13F8E5FB405CEA5F1ABC69E20D234CD69A7BCD4B5BB5B437D772DCB7CC32A01203313C8538C0180391ECDF0BA54D43FE08C3F142DACC6DBCD3BE2169B77A8375DF6D24091C4BED89431FC6BE48AF99C050FACE23195652719F37B34D5AF18C6316AD74D6F272DBAAD343D0AD3F670A514AEADCD6E8DB6F7F9248FD07F0E7FC13DBF653F167C0AF117C49B1F8CDF1065F07F856F21B0D4AF0E89B5A09A66458D44660DED92E9CAA9033EC6BE56FDADBC0DF06FC11AC68B1FC1FF001BF88BC6B67710CADA949AAE98D646D5C32F961372216DC0B67E5E303939C0F71FD9FBFE509DF1EFFEC6CD23FF00475A57C615C9C3F87C4CB135E55B13526A9547049F2D9AE48BD6D14EF793D9AE9A77D31B3A6A9C1469A5CD1BDD5FBB5A6BE47D75E09F88563FB5CFFC13B3C4BE0CF13DF69CFE3BF827147AC783EEEEE454BBBAD1F245'
	$sHexData &= 'DD8AB9399162501D530480100E178F916BB9F0E7C0CB8F11FC05F1178F93C43E16B6B7F0E5FDB5849A45C5FECD56F0CDD24860C7CE8BDCE470AFD769AE1ABDCCB30D4A84EB4684AF1736F97F95B49C92F26FDEF59339311525350735ADB7EEBA7F97C8FDC2FF008236FF00C140752FDAF3C19AF7872FBC29A178757E1DE97A7C11CBA6B32C779BC4C99F2C8C4607920E013F78D7C2BFF0515FF82B56B3FB5BF80752F87377E05F0E68D6D63ABEE6D42399E7B83E43B01E5E4011962393F37048F7AF65FF00836BA743E23F8C506E1E74B63A632A77601AEC13F9B0FCEBF39BE31D9CBA77C5DF155BCF1BC53C1AC5DC72230C323099C1047A835F9E645C3996C789F1A9525FBAF67286AF46E376F7D6EFBDD1EDE331D5DE5F4BDEF8B993DB5B3D0FD6CFF823A6A3E18F0D7FC1287C61A9F8BAC6DEFBC31677BAA5D6AF6F244245BAB78E2467565FE20557183C76E95F37F8BFFE0E1EF8ADFF000934A3C29E16F01E87E1BB76D96361716535C491C23855775990138FEE2A81D2BD7FF603FF0094107C61FF00AF1D7FFF00496BF282AB21E1ECBF30CCF31AD8EA6AA38D56927B2D35696D77A6BE418DC756A187A11A32B5E3D0FD18FF008262FECEF65FF050FF00DA1FE207C77F8BB67A55DE89A35CFDA6E2C522F2EC2E6F0C61CEF424E628E35525493B8B0CE79CDCF8EDFF00070CF8B34BF1D5C6'
	$sHexData &= '9BF0A3C29E12D37C1DA631B7B27D52D25967BA8D7857091C91AC4840E1304818C9EC3B2FF821B88FE26FEC19F1BBC03A6DD44BE23BCB9B892388B7CC12E6C52189B1E85E2719F6AFCB7F12F86AFF00C1BE21BED2755B49EC352D3677B6BAB69976C904884AB2B0EC4106B6C065183CCF3AC5D2CC23CD1C3F2469D37F0C62E3BA5E7DFF00E0115B13570F84A52A0ECE77727D5BBED73F5EFE067C60F017FC174BE03F883C27E39F0CE99A07C4FF000DDA79B6F7F6684987770B716EED9758F7801E16661823939047C11FF04F9F01DE7C3DFF00829E780FC37AC408BA8687E287B1BA8C8CA8962F314E33DB72E41FA1AFA07FE0DD3F84FABEA5FB46F8AFC70239A1F0F689A249A64D3B29114B3CD244E10374255622C476DCA78C8CF9F7C17F1BD87C48FF0082E3D9EB7A590DA7DFF8EEE5A0704112283228718E30DB723D88AE7C3D28E07119A657846FD846973257BA84A51778AF5DEC5CE4EB430F88ABF1B95AFDD26B5FD0FB8BFE0A9DFF000536B3FD81FE20C1A5782FC2DA0EA9F123C47611DCDFEA5A844C63B5B45664855F615790921F6AEF0ABC9E49C1F29FD85BFE0B55A97ED5BF16AD3E17FC62F09784EF34AF1A16D3EDEE6CAD5D60F3187CB15C432BC8AE8FCAE4118257839247847FC1C27FF27EB6BFF62B58FF00E8DB9AF97BF63BFF0093B8F859FF00637E93FF00A5B0D6592F06'
	$sHexData &= 'E575F86E188A94EF5654F9B9EEF993B5D59DF44B4B2DB42F179AE2218F708CBDD52B5BA5BFE09EA7FF000564FD90F4EFD8EBF6B7D4345D062687C33AE5B26AFA5C24922D5246657841272423AB633FC256BED6F813E05F00FF00C11CFF00616D27E2CF8A3C3B6FE21F8A9E2E8633671CBB44D14B3279896B1B907CA4441BA47504920F5F9457947FC1C81FF2723E04FF00B179FF00F4A1ABD47FE0B6BE19BDF8E3FB05FC25F1F786637D53C3DA5AC3757925BFEF160867B65093363F84300A4E382E3A735854C756CCB2ECA70B8C9BF675DB551DEDCDCABDD8B7FDE7BF765468C70F5F135292F7A1F0F95F77F23C7FC31FF0719FC59B5F1AC373ACF84FC0B7DA019B33D8DAC1736F71E57A24CD3380C3D59083CF033C75DFF055CFD973C03FB40FECABA37ED35F0B74E8F4C5BF58A5D6ED6DE111ADCC72BEC32BC6BC2CD14A76B91D4124E76835F9915FB09E06F845ACFC1EFF00837DBC43A6F886DA4B4BFBDD16F35516D3290F6F1CF71E6C40AB0055B6156231C1635E8E7F95E0722C4E0F199645529CAA460E29E9384B7BAEB6D35F3F430C1622B6329D5A5887CC945B4DF46B63F1EEBD77F618FD962EBF6C9FDA67C3BE0586792CECEF9DAE351BA400B5B5A46374ACA0F1B88C28EBF338C822BC8ABEEEFF0083786357FDBBB54240253C237A57D8FDA6D07F226BEDB89B1F5705956231547E'
	$sHexData &= '28C5B5E4FA3F91E4E5F46357130A73D9B47BEFED97FF000536F0FF00FC13235083E0BFC09F06786E3BAF0F451FF6A5E5E46CF6F04ACA0EC2B1B2BCD395219E477E09030C738A7FB197FC15DB4EFDBB7C5A9F087E3C7833C29776BE2E736D61756B6CE2D5E6C656396395DCAB920EC951810DB4601F9ABE35FF0082B87C25D73E15FEDEBE3D6D62DAE23B7F105F1D574EB975C25DC12804329E876B650F7056B8FF00F827DFC20D6FE36FED91F0F748D0ADE596E2DB5AB5D4AE6545256D2DEDE6496495880768017009E37328EF5F134384B27A9912C6CFF88E1CEEADDF3735B9B9AF7E8FA7EA7AD3CCF14B19EC97C37B72DB4B5ED6B1B3FF00052AFD8EC7EC4DFB526ABE15B379A7F0F5E44BA968B2CA4B39B690902366C72C8CACA7D40527AD7EAF7ED17FB4BF85BF635FF827DFC30F1C6ADE18D3FC49AD69961A7C1E19B4B8402386FA4B223CC0D83B36C42424A8DD8C818CE6BE29FF00838AFC77A77883F6ADF0BE8B6AD1C97BA06823ED857AA34D2B32213EA14671FED8F5AF59FF0082CFFF00CA2DBE06FF00D7F699FF00A6B9EBCAC6B9E6D86C9BEBF76EA37CDAD9B56F2FE64B5F2674D2B61A78BF63F652B796BFA1E01E2EFF0082FE7C6FF18F87F57D32E34DF87D15A6AF6D25AB08F4A9CB408EA54942D3919C1FE20C3DABDFBF628F841F0FFF00E0995FB07DBFED05E3DD0E2D73C6FE'
	$sHexData &= '21823B8D2A095434B6EB367ECF043B87EEDD97E777C6E032390307F286BF597FE0A49E1EBBFDA4BFE08E5F0A3C5BE178A5BDD3FC311586A3A8451AEF78225B57B790B05E9E5BB7CC7A0009E8335EC71365183C1BC2E5D8582A347115146A38E9CC92D22DFF007B6397018AAB57DA57A8F9A508DE37D6DDDFC8F20B4FF838CFE2EC7E321753F84FC052E87E7EE6D3D20B9498C59FBA27F38E1F1FC5B08CF3B7B57A37FC1457F67FF87DFB747EC470FED2DF0D3488344D76CE3FB56BB6F1C4B1BDDC6ADB2E1660B853344DCF99D5957BE463F2CEBF59BF66DF0D5D7ECF3FF0411F88379E2C0FA68F1769FA8CBA7C17276B1176A20B7C03FF003D1B0C0752181A9E21C9B0592D4C2E372A8FB2A8EA46168B7EFC65BA6AFAFAFF00C01E071557171A94B12F9A3CADEBD1AD99F933451457EA47CE8514514005145140051451400514514005145140051451401F477FC13AFE367867C29E24F177C37F883A8369BF0F3E2EE95FD89A8DE900A69376AE1ECEF98120011499C93C00D93C035E75FB56FECADE2CFD8F7E316A5E0EF1659490CD6AE5ECAF56322D756B627E4B8818F0C8C3AE0E55B72B619481E6D5F4BFC06FF82893786BE14DB7C37F8AFE0CD33E2F7C3DB371FD9F6DA95C3C3A9682BB7611677432D1A85C613B6D001519AF9FC561B1585C4CB1B838F3A9DB9E1749B6B4528B765CD6D1A6'
	$sHexData &= 'D2692D535AF6D3A94EA5354AABB35B3FD1F975BAD8C7F861FB57681E09FF00827A7C4CF8497563AC4BE21F1A6B963A959DD451C66CA18A07859C48C5C3863E51C0542391922BC02BEAEB87FD8A35B9DAEF6FED21A2283B9EC21FEC99D587F76376248FAB9A7EB7FB687C19F825E0AD4745F82FF07619F52D56DDEDE6F147C405B7D5EFE247C0222B5DAD6E8703AF20E7953DF9B0B8C74E73FAA616A73549734B9AD149D92BB6DED64B48A93F234A94B992F6B52368AB2B6AFABDBE7D6C7837C6BD2FE1EE989E17FF00840754F126A6D3E876F2EBFF00DAF6F1C42DB5339F3A28367588718CE4FF00B47B70F4B2399246638058E4E0003F21C0A4AFA4A34DD3828393979BDFF438272E677B58F67FD857F6D3D7FF00617F8E1078BB46B74D4AD2784DA6A9A649298E3BFB7241DBB803B5C100AB60E0F6C122BEA1FDB87FE0A39FB347ED43F067C4E349F839AA58FC4CD7A3468F5B9F4AB1B7682E015CC8D7314C65930063053E6E01C57E7BD15E2637867038AC6C3309A71AB1B6B1938DD2774A56DD7FC31D74B1F5A9D274159C5F46AF6F43ED1FD98FFE0A55E13F82BFF04DCF885F06B53D0FC4373E22F1443A84161776AB0B5928BA8447BA566915D4A9C9C2A36401C8CF1F175145776072AC3E0EA56AB4159D59734B5EBB7C8C6B626756318CF68AB2F43D4BF643FDAEFC5DFB16FC5FB5F177'
	$sHexData &= '84AE23F3157C8BEB19F26DB52809C98A40083EE1872A4023B83F76F89BFE0A61FB1FFED4F343AEFC57F831AC41E2C48D64B99EDAD63945C3AF44F3E19A29251C71E6A018E2BF3028AF3F35E17C163EB2C54F9A1552B73C24E32B766D6FF337C36635A8C3D9AB38F66AE8FD05FDAA3FE0B4DA45E7C1293E1AFC02F04C9F0DFC33770B5BDC5E491436D7491BE43A43140CC91B367994BB31C9C60FCD5F217EC81F1B2CBF671FDA6BC17E38D46D2EAFEC3C37A925DDC416DB7CE923C156D9B8805B04900900E3A8EB5E6F456B80E1CC060F0B530942168D4BF336DB94AEACDB93D6FF0097426B63EB55A91AB37AC76ECADE47D19FF0544FDB13C3FF00B707ED383C67E19D3B59D33498B48B7D3523D51234B8768DA46662B1BBA8199303E63D3DF15E39F03BC7B07C2BF8D5E0FF00145D412DD5B786F5BB2D525862203CA904E92B2AE78C90A40CFAD72D45776132CA186C1C703497B918F2AD75B6DB98D5C44EA5575A5BB773EA8FF82B07EDDDE1AFDBCFE32E81AEF85749D734BD3345D2BEC27FB55228E7964323393B637750A32003BB279E057ACFEC19FF00050FF1F7EC4FF00EC34CF89FF0E3C47E29F823E206923D2EFA6B12042AF92F144D2810CF1372446CCBFC5B588040FCFEAFBE7F619FF82BAF87BC1BF03E1F83FF001CBC283C63E00861169677515BA4F2DAC00FCB14B0B101D13F85'
	$sHexData &= 'D4875006031C11F299F6454E86514F0186C37B6A506AF1E66A696BEF41FF00326FBEAB43D2C1E3253C54AB54A9C927D6DA5FB35D8ED2F3F6FBFD897E1B6A7FF097783FE04EA7A878B997CFB6B5BAB08A0B3B4997E64CAB4F245110C7EF4313118F615EF5FB42FC7BF11F8BFF00E08BBE30F1AFC48862D275AF1FC323595844ACAB6D0DCDC2A5A42AADF371105639EB96638CD78CE93E37FF0082717C36D563F1369DA76A9ACDFD91F3EDF4992D757B840E3240F2EE310BFD2472BD2BE66FF82957FC14EB58FDBCB5EB1D32C34E7F0D78074272FA7E965C34D71263689A72BF2EE0BC2A2F0A09E589CD7C8E1B21FED0C650F6187AD08D39294AA576F9AD1D54209B7A37BB5D91E9CF19EC294F9E706E49A5186DAF567ADFFC1353F6E2FD9F7F67DFD917C5FE18F889E1A173E27D45EE1A6FF8938BC6D7A164C47089082136F230E55467703926A8FF00C1BDD34571FB7D6B92410FD9E07F0A5F34716F2FE529BAB4C2E4F27038CF7AF82ABEB7FF00822E7ED21E0DFD983F6C0B9D7BC73ACA683A35F787AEB4E4BB9219248D26696091436C56201113738C671EB5F5BC43C3F1A396E61570BCF3A95D5DABB96ABA457FC3FDC79982C6B957A11A964A0F7DBEF67D4FFB447FC150BE17DFFC6FF1CFC2EFDA07E180F1AE8BE16D76E2DF48D56C608A5BA8232E70ACACF1B215040DF1C80B01CA923270'
	$sHexData &= '9BFE0B09F003F649F026A3A6FECF7F09AEAD35ABD8C47F6ED42D92DA1638F95A593CD92E26DA4FDC62A09CFCC339AF83BF6D5F899A47C64FDAC7E2078A3409DEEB45D6F599EEACA678DA333445BE56DAC0119C670403CF205797D6581E03CB6A61692ACA6935172A7CF25072B2BDE37DEFBAD0BAD9CD78D4972357BBB4ACAF6F537FE28FC4DD6FE337C43D5FC53E23BE9352D735CB96BABBB87EAEE7B01D9400000380001DABEB2FDBD3FE0A55E13FDAC7F630F865F0EB47D0FC43A76B9E119AD67D467BC5845A3186CE4B72B0B248CEE097CE591381D2BE2EA2BEAF1393616BD4A15671B3A2EF0B689696DBB58F329E2AA42338A7F1EE15F5B7FC139FFE0AB7AFFEC436573E17D634A1E30F879A948D24DA6492ED9AC99F87680B6570C325A361B58F3952493F24D15AE6795E1730C3BC2E321CD07D3F54D6A9F9A270F88A9426AA527667E9C41FB767EC23A66B4BE2EB6F81DAD7FC241BF72D8FF635BF931B02486F24DD7D940CF390B9E9C71C7CDDFF000517FF0082A4F89BF6EFBCB4D1EDEC07857C07A4CBE6DA69114BE63DCC8321659DC001881F7500DAB93F78F35F2C515E365FC1F80C2E2238A6E75271F85CE4E5CBFE1BE8BF33AEBE695AA41D3D229EF6495FD428A28AFA93CE0A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A'
	$sHexData &= '28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A'
	$sHexData &= '28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2800A28A2803FFFD9'

	Return $sHexData
EndFunc   ;==>_ImgEmptySlots

; =================================================================
; Helper function to deploy the file from hex data.
; =================================================================
Func _Deploy_ImgEmptySlots()
	Local $sHexData = _ImgEmptySlots()
	Local $Deploy_Dir = @TempDir
	If $sHexData = "" Then Return SetError(1, 0, False)
	Local $hFile = FileOpen($g_sImgEmptySlots, 2 + 8 + 16)
	If $hFile = -1 Then Return SetError(2, 0, False)
	FileWrite($hFile, Binary($sHexData))
	FileClose($hFile)
	If Not FileExists($g_sImgEmptySlots) Then Return SetError(3, 0, False)
	Return True
EndFunc   ;==>_Deploy_ImgEmptySlots


; === END UTILITY & HELPER FUNCTIONS ===
