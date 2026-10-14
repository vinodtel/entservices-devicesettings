"""
/**
 * @file TCID18_ResolutionCrossPluginConsistency.py
 * @brief L3 VideoPort scenario testcase.
 *
 * @testcase TCID18_ResolutionCrossPluginConsistency
 * @details Drives setCurrentResolution 720p -> 1080p (reversed when the baseline is
 *          720p). After each resolutionChanged event it cross-checks
 *          DisplaySettings.getCurrentResolution, the WxH of
 *          FrameRate.getDisplayFrameRate and getSupportedTvResolutions membership.
 *
 * @precondition
 *  - Display connected (otherwise SKIPPED); plugin stack active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - All plugins agree on the active resolution after every change.
 *
 * @pass_criteria
 *  - Events received and getters consistent; supported-list membership is a
 *    warning only.
 *
 * @failure_criteria
 *  - Set rejected, event missing, or getters disagree.
 */
"""

import time

from utils import SKIPPED, finish, log_warning
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID18_ResolutionCrossPluginConsistency"


def run_test():
    start = time.perf_counter()
    if vh.get_connected() is not True:
        return finish(TC, start, SKIPPED, "display not connected; setCurrentResolution requires HPD")

    baseline = vh.get_resolution_name()
    sequence = ["1080p", "720p"] if baseline == "720p" else ["720p", "1080p"]
    supported = vh.get_supported_tv_resolutions() or []

    listener = vh.open_listener(TC, [vc.EVT_RESOLUTION_CHANGED])
    if listener is None:
        return finish(TC, start, False, "event listener registration failed")
    try:
        for target in sequence:
            _, result = vh.call(vc.curl_set_current_resolution(target), f"setCurrentResolution {target}")
            if not vh.result_success(result):
                return finish(TC, start, False, f"setCurrentResolution {target} rejected")
            if vh.expect_event(listener, vc.EVT_RESOLUTION_CHANGED, vh.resolution_event_matches(target)) is None:
                return finish(TC, start, False, f"resolutionChanged not received for {target}")

            current = vh.get_resolution_name()
            if current != target:
                return finish(TC, start, False, f"getCurrentResolution returned {current}, expected {target}")

            raw = vh.get_display_framerate_raw()
            framerate = vh.parse_framerate(raw)
            if framerate is None:
                return finish(TC, start, False, f"getDisplayFrameRate '{raw}' not 'WxHpxR' (known gap #3)")
            expected_wh = vh.VIC_TABLE[target][:2]
            if framerate[:2] != expected_wh:
                return finish(TC, start, False,
                              f"FrameRate reports {framerate[0]}x{framerate[1]}, DisplaySettings reports {target}")

            if supported and target not in supported:
                log_warning(f"{target} not listed in supportedTvResolutions {supported}")
        return finish(TC, start, True)
    finally:
        listener.close()
        vh.restore_resolution(baseline)
