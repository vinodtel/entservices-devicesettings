"""
/**
 * @file TCID14_ResolutionChangeTriggersFrameRateEvent.py
 * @brief L3 VideoPort set + state + event testcase.
 *
 * @testcase TCID14_ResolutionChangeTriggersFrameRateEvent
 * @details Changes resolution across refresh rates (1080p -> 1080p50) via
 *          DisplaySettings.setCurrentResolution (AIDL: setProperty(VIC)). A single
 *          IHDMIOutputControllerListener.onFrameRateChanged callback must surface as
 *          both DisplaySettings.resolutionChanged and FrameRate.onDisplayFrameRateChanged.
 *
 * @precondition
 *  - Display connected (otherwise SKIPPED); plugin stack active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - Both events received; getCurrentResolution == 1080p50; getDisplayFrameRate rate 50.
 *
 * @pass_criteria
 *  - All of the above hold.
 *
 * @failure_criteria
 *  - Set rejected, either event missing, or getters disagree.
 */
"""

import time

from utils import SKIPPED, finish, log_info
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID14_ResolutionChangeTriggersFrameRateEvent"
FROM_RES = "1080p"
TO_RES = "1080p50"


def run_test():
    start = time.perf_counter()
    if vh.get_connected() is not True:
        return finish(TC, start, SKIPPED, "display not connected; setCurrentResolution requires HPD")

    baseline = vh.get_resolution_name()
    listener = None
    try:
        if baseline != FROM_RES:
            log_info(f"Moving to {FROM_RES} first (baseline {baseline})")
            _, result = vh.call(vc.curl_set_current_resolution(FROM_RES), f"setCurrentResolution {FROM_RES}")
            if not vh.result_success(result):
                return finish(TC, start, False, f"pre-step setCurrentResolution {FROM_RES} failed")
            time.sleep(2)

        listener = vh.open_listener(TC, [vc.EVT_RESOLUTION_CHANGED, vc.EVT_FRAMERATE_CHANGED,
                                         vc.EVT_RESOLUTION_PRECHANGE, vc.EVT_FRAMERATE_CHANGING])
        if listener is None:
            return finish(TC, start, False, "event listener registration failed")

        _, result = vh.call(vc.curl_set_current_resolution(TO_RES), f"setCurrentResolution {TO_RES}")
        if not vh.result_success(result):
            return finish(TC, start, False, f"setCurrentResolution {TO_RES} rejected")

        if vh.expect_event(listener, vc.EVT_RESOLUTION_CHANGED, vh.resolution_event_matches(TO_RES)) is None:
            return finish(TC, start, False, "resolutionChanged not received")
        if vh.expect_event(listener, vc.EVT_FRAMERATE_CHANGED) is None:
            return finish(TC, start, False, "FrameRate.onDisplayFrameRateChanged not received")
        vh.log_informational_events(listener, [vc.EVT_RESOLUTION_PRECHANGE, vc.EVT_FRAMERATE_CHANGING])

        current = vh.get_resolution_name()
        if current != TO_RES:
            return finish(TC, start, False, f"getCurrentResolution returned {current}, expected {TO_RES}")
        framerate = vh.get_display_framerate()
        if framerate is None or framerate[2] != 50:
            return finish(TC, start, False, f"getDisplayFrameRate reports {framerate}, expected rate 50")
        return finish(TC, start, True)
    finally:
        if listener:
            listener.close()
        vh.restore_resolution(baseline)
