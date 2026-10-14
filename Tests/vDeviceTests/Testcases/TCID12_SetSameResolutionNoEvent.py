"""
/**
 * @file TCID12_SetSameResolutionNoEvent.py
 * @brief L3 VideoPort set + state + event testcase.
 *
 * @testcase TCID12_SetSameResolutionNoEvent
 * @details Calls DisplaySettings.setCurrentResolution with the currently active
 *          resolution. dVideoPortAIDLImpl skips setProperty(VIC) and the
 *          pre/post callbacks when the VIC is already active, so no
 *          resolutionChanged must be emitted.
 *
 * @precondition
 *  - Display connected (otherwise SKIPPED); plugin stack active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - Set succeeds, no resolutionChanged within VIDEOPORT_NO_EVENT_WINDOW,
 *    resolution unchanged.
 *
 * @pass_criteria
 *  - All of the above hold.
 *
 * @failure_criteria
 *  - Set rejected, unexpected resolutionChanged, or resolution changed.
 */
"""

import time

from utils import SKIPPED, finish
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID12_SetSameResolutionNoEvent"


def run_test():
    start = time.perf_counter()
    if vh.get_connected() is not True:
        return finish(TC, start, SKIPPED, "display not connected; setCurrentResolution requires HPD")

    current = vh.get_resolution_name()
    if not current:
        return finish(TC, start, False, "getCurrentResolution failed")

    listener = vh.open_listener(TC, [vc.EVT_RESOLUTION_CHANGED])
    if listener is None:
        return finish(TC, start, False, "event listener registration failed")
    try:
        _, result = vh.call(vc.curl_set_current_resolution(current), f"setCurrentResolution {current} (same)")
        if not vh.result_success(result):
            return finish(TC, start, False, f"setCurrentResolution {current} (same value) rejected")
        if not vh.expect_no_event(listener, vc.EVT_RESOLUTION_CHANGED):
            return finish(TC, start, False, "resolutionChanged emitted for an unchanged VIC")
        after = vh.get_resolution_name()
        if after != current:
            return finish(TC, start, False, f"resolution changed from {current} to {after}")
        return finish(TC, start, True)
    finally:
        listener.close()
