"""
/**
 * @file VideoPort_Helpers.py
 * @brief Response parsers and shared helpers for the VideoPort L3 suite.
 *
 * @testcase VideoPort_Helpers
 * @details Wraps curl dispatch + JSON-RPC result extraction for every getter in
 *          VideoPort_Curl.py, normalises the varying result shapes (bare values
 *          vs. objects), and provides the resolution (VIC) table used by the
 *          AIDL implementation plus event-listener convenience helpers.
 */
"""

import json
import re

import VideoPort_Curl as vc
from utils import (
    EVENT_TIMEOUT,
    NO_EVENT_WINDOW,
    JsonRpcEventListener,
    log_error,
    log_info,
    log_warning,
    parse_result,
    send_curl_command,
)

# Resolution names supported by dVideoPortAIDLImpl.cpp (kVicTable):
# name -> (width, height, refresh rate, progressive)
VIC_TABLE = {
    "480p": (720, 480, 60, True),
    "576p50": (720, 576, 50, True),
    "720p": (1280, 720, 60, True),
    "720p50": (1280, 720, 50, True),
    "1080i": (1920, 1080, 60, False),
    "1080i50": (1920, 1080, 50, False),
    "1080p": (1920, 1080, 60, True),
    "1080p24": (1920, 1080, 24, True),
    "1080p25": (1920, 1080, 25, True),
    "1080p30": (1920, 1080, 30, True),
    "1080p50": (1920, 1080, 50, True),
    "2160p24": (3840, 2160, 24, True),
    "2160p25": (3840, 2160, 25, True),
    "2160p30": (3840, 2160, 30, True),
    "2160p50": (3840, 2160, 50, True),
    "2160p": (3840, 2160, 60, True),
}

HDR_SETTINGS = {"HdrOff", "Hdr10", "Hdr10Plus", "HdrHlg", "HdrDolbyvision", "HdrTechnicolor"}
EOTF_VALUES = {"EOTF_UNKNOWN", "EOTF_OTHER", "EOTF_BT1886", "EOTF_BT2100", "EOTF_SMPTE_ST_2084"}

HDR_CAP_BITS = {
    0x01: "HDR10",
    0x02: "HLG",
    0x04: "DolbyVision",
    0x08: "TechnicolorPrime",
    0x10: "HDR10+",
    0x20: "SDR",
}
HDR_CAP_MASK = 0x3F

_FRAMERATE_RE = re.compile(r"^(\d+)x(\d+)p?x(\d+(?:\.\d+)?)$")


def call(cmd, label):
    """Send a curl command, log request/response, return (raw_response, result)."""
    raw = send_curl_command(cmd)
    log_info(f"{label} -> {raw}")
    return raw, parse_result(raw)


def result_success(result):
    """True when a result is present and does not report success == false."""
    if result is None:
        return False
    if isinstance(result, dict):
        return result.get("success", True) is not False
    return True


def _unwrap(result, key):
    """DisplayInfo/DeviceInfo properties may return a bare value or {key: value}."""
    if isinstance(result, dict):
        return result.get(key)
    return result


# ---------------------------------------------------------------------------
# Getters (return None on failure)
# ---------------------------------------------------------------------------
def get_connected():
    _, result = call(vc.CURL_DISPLAY_CONNECTED, "DisplayInfo.connected")
    value = _unwrap(result, "connected")
    return value if isinstance(value, bool) else None


def get_current_resolution():
    _, result = call(vc.CURL_GET_CURRENT_RESOLUTION, "getCurrentResolution")
    if isinstance(result, dict) and isinstance(result.get("resolution"), str):
        return result
    return None


def get_resolution_name():
    result = get_current_resolution()
    return result.get("resolution") if result else None


def get_active_input():
    _, result = call(vc.CURL_GET_ACTIVE_INPUT, "getActiveInput")
    if isinstance(result, dict) and isinstance(result.get("activeInput"), bool):
        return result["activeInput"]
    return None


def get_hdr_capabilities():
    _, result = call(vc.CURL_GET_TV_HDR_CAPABILITIES, "getTVHDRCapabilities")
    value = _unwrap(result, "capabilities")
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def get_supported_tv_resolutions():
    _, result = call(vc.CURL_GET_SUPPORTED_TV_RESOLUTIONS, "getSupportedTvResolutions")
    value = _unwrap(result, "supportedTvResolutions")
    return value if isinstance(value, list) else None


def get_hdr_setting():
    _, result = call(vc.CURL_HDR_SETTING, "DisplayInfo.hdrsetting")
    value = _unwrap(result, "hdrsetting")
    return value if isinstance(value, str) else None


def get_eotf():
    _, result = call(vc.CURL_EOTF, "DisplayInfo.eotf")
    value = _unwrap(result, "eotf")
    return value if isinstance(value, str) else None


def get_settop_hdr_support():
    _, result = call(vc.CURL_GET_SETTOP_HDR_SUPPORT, "getSettopHDRSupport")
    return result if isinstance(result, dict) else None


def get_zoom_setting():
    _, result = call(vc.CURL_GET_ZOOM_SETTING, "getZoomSetting")
    value = _unwrap(result, "zoomSetting")
    return value if isinstance(value, str) else None


def get_frm_mode():
    _, result = call(vc.CURL_GET_FRM_MODE, "getFrmMode")
    value = _unwrap(result, "auto-frm-mode")
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def get_display_framerate_raw():
    _, result = call(vc.CURL_GET_DISPLAY_FRAMERATE, "getDisplayFrameRate")
    value = _unwrap(result, "framerate")
    return value if isinstance(value, str) else None


def get_display_framerate():
    """Return (width, height, rate) parsed from getDisplayFrameRate, or None."""
    return parse_framerate(get_display_framerate_raw())


def get_hdcp_status():
    _, result = call(vc.CURL_GET_HDCP_STATUS, "getHDCPStatus")
    if isinstance(result, dict) and isinstance(result.get("HDCPStatus"), dict):
        return result["HDCPStatus"]
    return None


def get_supported_hdcp():
    _, result = call(vc.CURL_SUPPORTED_HDCP, "DeviceInfo.supportedhdcp")
    value = _unwrap(result, "supportedHDCPVersion")
    return value if isinstance(value, str) else None


# ---------------------------------------------------------------------------
# Parsers / normalisers
# ---------------------------------------------------------------------------
def parse_framerate(value):
    """Parse "WxHpxR" (getter) or "WxHxR" (event payload) into (w, h, rate)."""
    if not isinstance(value, str):
        return None
    match = _FRAMERATE_RE.match(value.strip())
    if not match:
        return None
    rate = float(match.group(3))
    return int(match.group(1)), int(match.group(2)), int(rate) if rate.is_integer() else rate


def as_int(value):
    """Coerce event/result numbers that may arrive as strings; None if not numeric."""
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def resolution_event_matches(name):
    """Predicate for resolutionChanged params matching VIC table entry `name`."""
    width, height = VIC_TABLE[name][:2]

    def predicate(params):
        if as_int(params.get("width")) != width or as_int(params.get("height")) != height:
            return False
        reported = params.get("resolution")
        return not reported or str(reported).startswith(name[:4])
    return predicate


def format_framerate(width, height, rate):
    return f"{width}x{height}px{rate}"


def normalize_hdcp(value):
    """Map 'hdcp_20' / 'HDCP_2.2' / '2.2' / 'unavailable' to '2.0' / '2.2' / 'unavailable'."""
    if not isinstance(value, str):
        return None
    text = value.strip().lower().replace("hdcp", "").strip("_ ")
    if text in ("", "unavailable", "none"):
        return "unavailable"
    if re.fullmatch(r"\d\.\d", text):
        return text
    if re.fullmatch(r"\d\d", text):
        return f"{text[0]}.{text[1]}"
    return text


def decode_hdr_caps(mask):
    return [name for bit, name in HDR_CAP_BITS.items() if mask & bit]


def alternate_resolution(current, preferred="1080p", fallback="720p"):
    """Pick a target resolution different from current."""
    return fallback if current == preferred else preferred


# ---------------------------------------------------------------------------
# Event listener helpers
# ---------------------------------------------------------------------------
def open_listener(listener_id, events=None):
    """Open a JsonRpcEventListener subscribed to `events` (default: all suite events)."""
    listener = JsonRpcEventListener(events or vc.ALL_EVENTS, listener_id, timeout=EVENT_TIMEOUT)
    if not listener.connect():
        log_error("Unable to register event listener")
        return None
    return listener


def expect_event(listener, evt, predicate=None, timeout=EVENT_TIMEOUT):
    """Wait for (callsign, event); log and return params or None."""
    params = listener.wait_for_event(evt[0], evt[1], predicate, timeout)
    if params is None:
        log_error(f"Expected event {evt[0]}.{evt[1]} not received within {timeout}s")
    else:
        log_info(f"Received {evt[0]}.{evt[1]}: {json.dumps(params)}")
    return params


def expect_no_event(listener, evt, window=NO_EVENT_WINDOW):
    """Return True when (callsign, event) is NOT received within window."""
    params = listener.wait_for_event(evt[0], evt[1], None, window)
    if params is not None:
        log_error(f"Unexpected event {evt[0]}.{evt[1]}: {json.dumps(params)}")
        return False
    log_info(f"No {evt[0]}.{evt[1]} event within {window}s (as expected)")
    return True


def log_informational_events(listener, events):
    """Log presence/absence of events that are not asserted (per mapping doc)."""
    for callsign, event in events:
        seen = listener.events(callsign, event)
        if seen:
            log_info(f"[info] {callsign}.{event} observed {len(seen)}x: {[e['params'] for e in seen]}")
        else:
            log_warning(f"[info] {callsign}.{event} not observed (not asserted per AIDL mapping)")


def restore_resolution(name):
    """Best-effort restore of a baseline resolution."""
    if not name:
        return
    current = get_resolution_name()
    if current == name:
        return
    raw, result = call(vc.curl_set_current_resolution(name), f"Restore resolution {name}")
    if not result_success(result):
        log_warning(f"Failed to restore resolution {name}: {raw}")
