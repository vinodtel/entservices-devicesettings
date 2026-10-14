"""
/**
 * @file TCID21_NegativeAndBoundary.py
 * @brief L3 VideoPort negative / boundary testcase.
 *
 * @testcase TCID21_NegativeAndBoundary
 * @details Sends invalid requests and verifies each is rejected (JSON-RPC error or
 *          success:false), that no post-change events (resolutionChanged,
 *          onDisplayFrameRateChanged) are emitted and that the state is unchanged:
 *            - setCurrentResolution videoDisplay "HDMI9"
 *            - setCurrentResolution resolution "999p"
 *            - setCurrentResolution without resolution
 *            - setFrmMode 5
 *            - setZoomSetting "BOGUS"
 *            - setDisplayFrameRate "" and "abc"
 *          setForceHDRMode without params is also sent; hdr_mode is optional per the
 *          API docs, so it only has to respond (not asserted as rejected).
 *
 * @precondition
 *  - Plugin stack active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - All invalid requests rejected without side effects.
 *
 * @pass_criteria
 *  - Every request rejected, no post-change events, state unchanged.
 *
 * @failure_criteria
 *  - A request accepted / unanswered, a post-change event emitted, or state changed.
 */
"""

import time

from utils import NO_EVENT_WINDOW, finish, is_rejected, log_error, log_info, responded
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID21_NegativeAndBoundary"

NEGATIVE_CASES = [
    ("setCurrentResolution videoDisplay=HDMI9", vc.curl_set_current_resolution("1080p", video_display="HDMI9")),
    ("setCurrentResolution resolution=999p", vc.curl_set_current_resolution("999p")),
    ("setCurrentResolution missing resolution", vc.curl_set_current_resolution(None)),
    ("setFrmMode 5", vc.curl_set_frm_mode(5)),
    ("setZoomSetting BOGUS", vc.curl_set_zoom_setting("BOGUS")),
    ("setDisplayFrameRate ''", vc.curl_set_display_framerate("")),
    ("setDisplayFrameRate 'abc'", vc.curl_set_display_framerate("abc")),
]


def _snapshot():
    return {
        "resolution": vh.get_resolution_name(),
        "zoom": vh.get_zoom_setting(),
        "frm": vh.get_frm_mode(),
        "framerate": vh.get_display_framerate_raw(),
    }


def run_test():
    start = time.perf_counter()
    before = _snapshot()
    log_info(f"State before: {before}")

    listener = vh.open_listener(TC, [vc.EVT_RESOLUTION_CHANGED, vc.EVT_FRAMERATE_CHANGED])
    if listener is None:
        return finish(TC, start, False, "event listener registration failed")
    failures = []
    try:
        for label, cmd in NEGATIVE_CASES:
            raw, _ = vh.call(cmd, label)
            if not responded(raw):
                failures.append(f"{label}: no response")
            elif not is_rejected(raw):
                failures.append(f"{label}: accepted")

        listener.collect(NO_EVENT_WINDOW)
        for evt in (vc.EVT_RESOLUTION_CHANGED, vc.EVT_FRAMERATE_CHANGED):
            seen = listener.events(*evt)
            if seen:
                failures.append(f"unexpected {evt[0]}.{evt[1]}: {[e['params'] for e in seen]}")
    finally:
        listener.close()

    after = _snapshot()
    log_info(f"State after: {after}")
    for key, value in before.items():
        if after[key] != value:
            failures.append(f"state '{key}' changed from {value} to {after[key]}")

    baseline_hdr = vh.get_hdr_setting()
    raw, _ = vh.call(vc.curl_set_force_hdr_mode(), "setForceHDRMode (no params)")
    if not responded(raw):
        failures.append("setForceHDRMode (no params): no response")
    if baseline_hdr == "HdrOff":
        vh.call(vc.curl_set_force_hdr_mode(False), "Restore setForceHDRMode false")

    for failure in failures:
        log_error(f"  - {failure}")
    return finish(TC, start, not failures, f"{len(failures)} negative check(s) failed")
