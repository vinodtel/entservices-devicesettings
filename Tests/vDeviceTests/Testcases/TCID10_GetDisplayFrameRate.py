"""
/**
 * @file TCID10_GetDisplayFrameRate.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID10_GetDisplayFrameRate
 * @details Reads org.rdk.FrameRate.getDisplayFrameRate (AIDL: IPanelOutput.getRefreshRate
 *          + getVideoResolution) and checks the "WxHpxR" format and that WxH agrees
 *          with DisplaySettings.getCurrentResolution.
 *
 * @note Known gap #3: GetCurrentDisplayFrameRate returns only the rate (e.g. "60"),
 *       which fails the format check.
 *
 * @precondition
 *  - org.rdk.FrameRate, org.rdk.DisplaySettings and org.rdk.DeviceSettings are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - framerate matches ^\\d+x\\d+p?x\\d+$ and WxH equals the current resolution.
 *
 * @pass_criteria
 *  - Format valid and WxH consistent.
 *
 * @failure_criteria
 *  - Missing/malformed framerate or WxH mismatch.
 */
"""

import time

from utils import finish, log_info
import VideoPort_Helpers as vh

TC = "TCID10_GetDisplayFrameRate"


def run_test():
    start = time.perf_counter()
    raw = vh.get_display_framerate_raw()
    if raw is None:
        return finish(TC, start, False, "getDisplayFrameRate did not return a framerate string")
    parsed = vh.parse_framerate(raw)
    if parsed is None:
        return finish(TC, start, False,
                      f"framerate '{raw}' is not 'WxHpxR' (known gap #3: GetCurrentDisplayFrameRate returns rate only)")
    width, height, rate = parsed
    log_info(f"Display frame rate: {width}x{height} @ {rate}")

    current = vh.get_resolution_name()
    if current in vh.VIC_TABLE:
        exp_w, exp_h = vh.VIC_TABLE[current][:2]
        if (width, height) != (exp_w, exp_h):
            return finish(TC, start, False,
                          f"framerate {width}x{height} disagrees with resolution {current} ({exp_w}x{exp_h})")
    return finish(TC, start, True)
