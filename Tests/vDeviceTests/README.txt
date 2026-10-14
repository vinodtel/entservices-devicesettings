VideoPort / VideoDevice (HDMI Output) L3 Test Suite
===================================================

Curl / JSON-RPC driven L3 tests for the DeviceSettings VideoPort and VideoDevice
AIDL implementation (plugin/hal/dVideoPortAIDLImpl.cpp, dVideoDeviceAIDLImpl.cpp),
exercised through the client plugins DisplaySettings, FrameRate, DisplayInfo,
DeviceInfo and HdcpProfile. Every set API is followed by a getter to verify the
state change and, where the AIDL mapping document lists a listener, by an
event assertion captured with a WebSocket JSON-RPC event listener.

Scenario specification: VideoPort_L3_TestScenarios.md


Layout
------
  SuiteManager.py              Suite runner (plugin lifecycle, init, per-test results)
  utils.py                     JSON-RPC/curl helpers, plugin control, multi-event listener
  VideoPort_Curl.py            Curl builders for every API in curlcommands.txt
  VideoPort_Helpers.py         Getters/parsers, VIC table, event helpers
  Init_VideoPort_Populate.py   Reachability check + baseline state logging
  Testcases/TCID01..TCID21     Test cases


Execution
---------
  cd Tests/L3Tests
  python3 SuiteManager.py videoport                 # full suite
  python3 SuiteManager.py videoport -t              # with timing output
  python3 SuiteManager.py videoport --test TCID11_SetCurrentResolution --test TCID14_ResolutionChangeTriggersFrameRateEvent

For every testcase SuiteManager activates org.rdk.DeviceSettings, then
org.rdk.DisplaySettings, org.rdk.FrameRate, DisplayInfo, DeviceInfo and
org.rdk.HdcpProfile, runs Init_VideoPort_Populate, runs the test and
deactivates the plugins in reverse order.

A test returns PASS, FAIL or SKIP. SKIP is used when a precondition (a connected
display for resolution / frame-rate / HDR set APIs) is not met; skips do not
fail the suite.


Environment variables
---------------------
  TARGET_HOST                  Device IP (default 127.0.0.1)
  JSONRPC_PORT                 Thunder port (default 9998)
  WPEFRAMEWORK_JSONRPC_URL     Full endpoint override (e.g. http://<ip>:9998/jsonrpc)
  AUTO_ACTIVATE_PLUGINS=0      Do not activate/deactivate plugins around each test
  VIDEOPORT_PLUGIN_INIT_WAIT   Seconds to wait after each plugin activation (default 3)
  VIDEOPORT_EVENT_TIMEOUT      Seconds to wait for an expected event (default 8)
  VIDEOPORT_NO_EVENT_WINDOW    Observation window for "no event" checks (default 4)
  VIDEOPORT_TIMING_ENABLED     Same as -t


Test cases
----------
  A. Read-only
    TCID01_DisplayConnected               DisplayInfo.connected
    TCID02_GetCurrentResolution           getCurrentResolution vs VIC table
    TCID03_GetHDCPStatus                  HdcpProfile.getHDCPStatus fields, isConnected == HPD
    TCID04_SupportedHdcp                  DeviceInfo.supportedhdcp vs HdcpProfile
    TCID05_GetActiveInput                 getActiveInput vs HPD
    TCID06_GetTVHDRCapabilities           bitmask within 0x3F
    TCID07_GetSupportedTvResolutions      non-empty list when connected
    TCID08_HdrSettingAndEotf              enums + hdrsetting/eotf consistency
    TCID09_GetSettopHDRSupport            standards[] vs supportsHDR
    TCID10_GetDisplayFrameRate            "WxHpxR" format, WxH vs current resolution
  B. Set API + state + event
    TCID11_SetCurrentResolution           -> resolutionChanged (asserted)
    TCID12_SetSameResolutionNoEvent       same VIC -> no resolutionChanged
    TCID13_SetDisplayFrameRate            -> FrameRate.onDisplayFrameRateChanged (asserted)
    TCID14_ResolutionChangeTriggersFrameRateEvent  1080p -> 1080p50, both events asserted
    TCID15_SetForceHDRMode                -> videoFormatChanged (asserted, HDR-capable TV)
    TCID16_SetZoomSetting                 NONE/FULL via getter (zoomSettingUpdated logged)
    TCID17_SetFrmMode                     1/0 via getter
  C. Scenario / persistence / negative
    TCID18_ResolutionCrossPluginConsistency
    TCID19_ResolutionPersistence          survives DeviceSettings/DisplaySettings/FrameRate restart
    TCID20_ZoomPersistence                survives plugin restart
    TCID21_NegativeAndBoundary            invalid inputs rejected, no events, state unchanged


AIDL event mapping (from VideoPort_ VideoDevice_API_Mapping.pdf)
----------------------------------------------------------------
  Asserted:
    IHDMIOutputControllerListener.onFrameRateChanged -> DisplaySettings.resolutionChanged
                                                       FrameRate.onDisplayFrameRateChanged
    IPanelOutputListener.onVideoFormatChanged        -> DisplaySettings.videoFormatChanged
  Logged only:
    resolutionPreChange, onDisplayFrameRateChanging  (PRECHANGE is NA/Deprecated)
    zoomSettingUpdated                               (IPlaneControl has no listener)
  Not triggerable by curl:
    IHDMIOutputControllerListener.onHDCPStatusChanged (checked passively in TCID03)


Known AIDL implementation gaps (expected failures)
--------------------------------------------------
  1. SetFRFMode/GetFRFMode return ERROR_UNAVAILABLE                         -> TCID17
  2. SetDisplayFrameRate uses atoi("WxHpxR") -> BAD_REQUEST                 -> TCID13
  3. GetCurrentDisplayFrameRate returns only the rate ("60")                -> TCID10, 13, 14, 18
  4. IPanelOutputListener is not registered, videoFormatChanged may not fire -> TCID15
  5. Zoom is cache/persistence only (IPlaneControl not wired)               -> TCID16 (getter only)
Failures caused by these gaps carry an explicit "known gap #N" reason.
