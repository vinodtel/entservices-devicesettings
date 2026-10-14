"""
/**
 * @file VideoPort_Curl.py
 * @brief VideoPort / VideoDevice (HDMI Output) JSON-RPC curl command library.
 *
 * @testcase VideoPort_Curl
 * @details Central library of curl command builders for every JSON-RPC method
 *          listed in curlcommands.txt that reaches the DeviceSettings VideoPort /
 *          VideoDevice AIDL implementation (via DisplaySettings, FrameRate,
 *          DisplayInfo, DeviceInfo and HdcpProfile). Static getters are exposed
 *          as ready-to-send command lists; setters are builder functions.
 *
 * @note Method names and parameter spelling are kept identical to
 *       curlcommands.txt (single source of truth for the suite).
 */
"""

import json

from utils import WPEFRAMEWORK_JSONRPC_URL

# ---------------------------------------------------------------------------
# Plugin callsigns
# ---------------------------------------------------------------------------
DEVICESETTINGS = "org.rdk.DeviceSettings"
DISPLAYSETTINGS = "org.rdk.DisplaySettings"
FRAMERATE = "org.rdk.FrameRate"
DISPLAYINFO = "DisplayInfo"
DEVICEINFO = "DeviceInfo"
HDCPPROFILE = "org.rdk.HdcpProfile"

# Plugins whose restart forces the VideoPort/VideoDevice HAL to reload persisted state.
RESTART_STACK = [DEVICESETTINGS, DISPLAYSETTINGS, FRAMERATE]

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
VIDEO_DISPLAY = "HDMI0"

ZOOM_NONE = "NONE"
ZOOM_FULL = "FULL"

FRM_MODE_DISABLED = 0
FRM_MODE_ENABLED = 1

# Events (callsign, event). Asserted per the AIDL mapping document:
#   resolutionChanged / onDisplayFrameRateChanged <- IHDMIOutputControllerListener.onFrameRateChanged
#   videoFormatChanged                            <- IPanelOutputListener.onVideoFormatChanged
EVT_RESOLUTION_CHANGED = (DISPLAYSETTINGS, "resolutionChanged")
EVT_VIDEO_FORMAT_CHANGED = (DISPLAYSETTINGS, "videoFormatChanged")
EVT_FRAMERATE_CHANGED = (FRAMERATE, "onDisplayFrameRateChanged")
# Logged only (PRECHANGE is NA/Deprecated in the mapping; IPlaneControl has no listener).
EVT_RESOLUTION_PRECHANGE = (DISPLAYSETTINGS, "resolutionPreChange")
EVT_FRAMERATE_CHANGING = (FRAMERATE, "onDisplayFrameRateChanging")
EVT_ZOOM_UPDATED = (DISPLAYSETTINGS, "zoomSettingUpdated")

ALL_EVENTS = [
    EVT_RESOLUTION_CHANGED,
    EVT_RESOLUTION_PRECHANGE,
    EVT_VIDEO_FORMAT_CHANGED,
    EVT_ZOOM_UPDATED,
    EVT_FRAMERATE_CHANGED,
    EVT_FRAMERATE_CHANGING,
]


def _curl(full_method, params=None, timeout=10, request_id=42):
    """Build a curl command list for a fully-qualified JSON-RPC method."""
    payload = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": full_method,
    }
    if params is not None:
        payload["params"] = params
    return [
        "curl",
        "-sS",
        "--max-time", str(timeout),
        "-H", "Content-Type: application/json",
        "-X", "POST",
        "--data-raw", json.dumps(payload),
        WPEFRAMEWORK_JSONRPC_URL,
    ]


# ---------------------------------------------------------------------------
# DisplayInfo
# ---------------------------------------------------------------------------
CURL_DISPLAY_CONNECTED = _curl(f"{DISPLAYINFO}.connected")
CURL_HDR_SETTING = _curl(f"{DISPLAYINFO}.hdrsetting")
CURL_EOTF = _curl(f"{DISPLAYINFO}.eotf")

# ---------------------------------------------------------------------------
# DeviceInfo / HdcpProfile
# ---------------------------------------------------------------------------
CURL_SUPPORTED_HDCP = _curl(f"{DEVICEINFO}.supportedhdcp", {"videoDisplay": VIDEO_DISPLAY})
CURL_GET_HDCP_STATUS = _curl(f"{HDCPPROFILE}.getHDCPStatus")

# ---------------------------------------------------------------------------
# DisplaySettings
# ---------------------------------------------------------------------------
CURL_GET_CURRENT_RESOLUTION = _curl(
    f"{DISPLAYSETTINGS}.getCurrentResolution", {"videoDisplay": VIDEO_DISPLAY}
)
CURL_GET_ACTIVE_INPUT = _curl(
    f"{DISPLAYSETTINGS}.getActiveInput", {"videoDisplay": VIDEO_DISPLAY}
)
CURL_GET_TV_HDR_CAPABILITIES = _curl(f"{DISPLAYSETTINGS}.getTVHDRCapabilities")
CURL_GET_SUPPORTED_TV_RESOLUTIONS = _curl(
    f"{DISPLAYSETTINGS}.getSupportedTvResolutions", {"videoDisplay": VIDEO_DISPLAY}
)
CURL_GET_SETTOP_HDR_SUPPORT = _curl(f"{DISPLAYSETTINGS}.getSettopHDRSupport")
CURL_GET_ZOOM_SETTING = _curl(f"{DISPLAYSETTINGS}.1.getZoomSetting")


def curl_set_current_resolution(resolution, video_display=VIDEO_DISPLAY, persist=True, ignore_edid=True):
    params = {"persist": persist, "ignoreEdid": ignore_edid}
    if video_display is not None:
        params["videoDisplay"] = video_display
    if resolution is not None:
        params["resolution"] = resolution
    return _curl(f"{DISPLAYSETTINGS}.setCurrentResolution", params)


def curl_set_force_hdr_mode(hdr_mode=None):
    """hdr_mode is optional per the API; None sends the request without params."""
    if hdr_mode is None:
        return _curl(f"{DISPLAYSETTINGS}.setForceHDRMode")
    return _curl(f"{DISPLAYSETTINGS}.setForceHDRMode", {"hdr_mode": hdr_mode})


def curl_set_zoom_setting(zoom_setting):
    return _curl(f"{DISPLAYSETTINGS}.setZoomSetting", {"zoomSetting": zoom_setting})


# ---------------------------------------------------------------------------
# FrameRate
# ---------------------------------------------------------------------------
CURL_GET_FRM_MODE = _curl(f"{FRAMERATE}.getFrmMode")
CURL_GET_DISPLAY_FRAMERATE = _curl(f"{FRAMERATE}.getDisplayFrameRate")


def curl_set_frm_mode(frmmode):
    return _curl(f"{FRAMERATE}.setFrmMode", {"frmmode": frmmode})


def curl_set_display_framerate(framerate):
    return _curl(f"{FRAMERATE}.setDisplayFrameRate", {"framerate": framerate})
