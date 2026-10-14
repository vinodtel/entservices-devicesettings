"""
/**
 * @file TCID02_GetCurrentResolution.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID02_GetCurrentResolution
 * @details Reads DisplaySettings.getCurrentResolution {videoDisplay: HDMI0}
 *          (AIDL: IHDMIOutput.getProperty(VIC)) and validates the resolution name
 *          against the VIC table of dVideoPortAIDLImpl and the reported w/h.
 *
 * @precondition
 *  - org.rdk.DisplaySettings and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - success:true and a resolution name known to the VIC table.
 *
 * @pass_criteria
 *  - Resolution is in VIC_TABLE and w/h (when reported) match the table.
 *
 * @failure_criteria
 *  - success:false, unknown resolution name, or inconsistent w/h.
 */
"""

import time

from utils import finish, log_info
import VideoPort_Helpers as vh

TC = "TCID02_GetCurrentResolution"


def run_test():
    start = time.perf_counter()
    result = vh.get_current_resolution()
    if result is None or not vh.result_success(result):
        return finish(TC, start, False, "getCurrentResolution failed")

    name = result["resolution"]
    if name not in vh.VIC_TABLE:
        return finish(TC, start, False, f"Resolution '{name}' not in VIC table {list(vh.VIC_TABLE)}")

    width, height, rate, progressive = vh.VIC_TABLE[name]
    log_info(f"Current resolution {name}: expected {width}x{height}@{rate} progressive={progressive}")
    if "w" in result and "h" in result:
        if (int(result["w"]), int(result["h"])) != (width, height):
            return finish(TC, start, False,
                          f"w/h {result['w']}x{result['h']} inconsistent with {name} ({width}x{height})")
    if "progressive" in result and bool(result["progressive"]) != progressive:
        return finish(TC, start, False, f"progressive={result['progressive']} inconsistent with {name}")
    return finish(TC, start, True)
