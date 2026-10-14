"""
/**
 * @file TCID13_SetDisplayFrameRate.py
 * @brief L3 VideoPort set + state + event testcase.
 *
 * @testcase TCID13_SetDisplayFrameRate
 * @details Sets org.rdk.FrameRate.setDisplayFrameRate {framerate:"WxHpxR"} with a
 *          rate valid for the current resolution family (50 if currently 60,
 *          otherwise 60) (AIDL: IPanelOutput.setRefreshRate), verifies
 *          getDisplayFrameRate and asserts FrameRate.onDisplayFrameRateChanged
 *          (AIDL: IHDMIOutputControllerListener.onFrameRateChanged).
 *          onDisplayFrameRateChanging is logged only (PRECHANGE deprecated).
 *
 * @note Known gaps: #2 SetDisplayFrameRate uses atoi("WxHpxR") -> BAD_REQUEST;
 *       #3 GetCurrentDisplayFrameRate returns rate only.
 *
 * @precondition
 *  - Display connected (otherwise SKIPPED); plugin stack active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - onDisplayFrameRateChanged received and getter reports the new rate.
 *
 * @pass_criteria
 *  - Set succeeds, event received, getter rate == requested rate.
 *
 * @failure_criteria
 *  - Baseline unreadable, set rejected, event missing or getter mismatch.
 */
"""

import time

from utils import SKIPPED, finish, log_warning
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID13_SetDisplayFrameRate"


def run_test():
    start = time.perf_counter()
    if vh.get_connected() is not True:
        return finish(TC, start, SKIPPED, "display not connected")

    baseline_raw = vh.get_display_framerate_raw()
    baseline = vh.parse_framerate(baseline_raw)
    if baseline is None:
        return finish(TC, start, False,
                      f"baseline framerate '{baseline_raw}' not 'WxHpxR' (known gap #3)")
    width, height, rate = baseline
    target_rate = 50 if rate == 60 else 60
    target = vh.format_framerate(width, height, target_rate)

    listener = vh.open_listener(TC, [vc.EVT_FRAMERATE_CHANGED, vc.EVT_FRAMERATE_CHANGING])
    if listener is None:
        return finish(TC, start, False, "event listener registration failed")
    changed = False
    try:
        _, result = vh.call(vc.curl_set_display_framerate(target), f"setDisplayFrameRate {target}")
        if not vh.result_success(result):
            return finish(TC, start, False,
                          f"setDisplayFrameRate {target} rejected (known gap #2: atoi() on 'WxHpxR')")
        changed = True

        def rate_matches(params):
            parsed = vh.parse_framerate(params.get("displayFrameRate"))
            return parsed is None or parsed[2] == target_rate

        params = vh.expect_event(listener, vc.EVT_FRAMERATE_CHANGED, rate_matches)
        if params is None:
            return finish(TC, start, False, "onDisplayFrameRateChanged (onFrameRateChanged) not received")
        if vh.parse_framerate(params.get("displayFrameRate")) is None:
            log_warning(f"Event displayFrameRate '{params.get('displayFrameRate')}' not in WxHxR form")
        vh.log_informational_events(listener, [vc.EVT_FRAMERATE_CHANGING])

        after = vh.get_display_framerate()
        if after is None or after[2] != target_rate:
            return finish(TC, start, False, f"getDisplayFrameRate reports {after}, expected rate {target_rate}")
        return finish(TC, start, True)
    finally:
        listener.close()
        if changed:
            vh.call(vc.curl_set_display_framerate(vh.format_framerate(width, height, rate)),
                    f"Restore framerate {baseline_raw}")
