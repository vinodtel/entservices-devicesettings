"""
/**
 * @file TCID11_SetCurrentResolution.py
 * @brief L3 VideoPort set + state + event testcase.
 *
 * @testcase TCID11_SetCurrentResolution
 * @details Sets HDMI0 to 1080p via DisplaySettings.setCurrentResolution
 *          {persist:true, ignoreEdid:true} (AIDL: IHDMIOutputController.setProperty(VIC)),
 *          verifies the getter and asserts DisplaySettings.resolutionChanged
 *          (AIDL: IHDMIOutputControllerListener.onFrameRateChanged -> post-change).
 *          If already at 1080p it first moves to 720p, since the same VIC skips the HAL.
 *          resolutionPreChange is logged only (PRECHANGE is NA/Deprecated).
 *
 * @precondition
 *  - Display connected (otherwise SKIPPED); plugin stack active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - resolutionChanged {width:1920, height:1080, resolution:1080p*}; getter == 1080p.
 *
 * @pass_criteria
 *  - Set succeeds, event received, getter and connection state as expected.
 *
 * @failure_criteria
 *  - Set rejected, event missing, getter mismatch or connection state changed.
 */
"""

import time

from utils import SKIPPED, finish, log_info
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID11_SetCurrentResolution"
TARGET = "1080p"
PRE_TARGET = "720p"


def run_test():
    start = time.perf_counter()
    if vh.get_connected() is not True:
        return finish(TC, start, SKIPPED, "display not connected; setCurrentResolution requires HPD")

    baseline = vh.get_resolution_name()
    listener = None
    try:
        if baseline == TARGET:
            log_info(f"Already at {TARGET}; moving to {PRE_TARGET} first")
            _, result = vh.call(vc.curl_set_current_resolution(PRE_TARGET), f"setCurrentResolution {PRE_TARGET}")
            if not vh.result_success(result):
                return finish(TC, start, False, f"pre-step setCurrentResolution {PRE_TARGET} failed")
            time.sleep(2)

        listener = vh.open_listener(TC, [vc.EVT_RESOLUTION_CHANGED, vc.EVT_RESOLUTION_PRECHANGE])
        if listener is None:
            return finish(TC, start, False, "event listener registration failed")

        _, result = vh.call(vc.curl_set_current_resolution(TARGET), f"setCurrentResolution {TARGET}")
        if not vh.result_success(result):
            return finish(TC, start, False, f"setCurrentResolution {TARGET} rejected")

        if vh.expect_event(listener, vc.EVT_RESOLUTION_CHANGED, vh.resolution_event_matches(TARGET)) is None:
            return finish(TC, start, False, "resolutionChanged (onFrameRateChanged) not received for 1920x1080")
        vh.log_informational_events(listener, [vc.EVT_RESOLUTION_PRECHANGE])

        current = vh.get_resolution_name()
        if current != TARGET:
            return finish(TC, start, False, f"getCurrentResolution returned {current}, expected {TARGET}")
        if vh.get_connected() is not True:
            return finish(TC, start, False, "DisplayInfo.connected changed after resolution set")
        return finish(TC, start, True)
    finally:
        if listener:
            listener.close()
        vh.restore_resolution(baseline)
