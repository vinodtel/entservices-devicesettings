"""
/**
 * @file TCID09_GetSettopHDRSupport.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID09_GetSettopHDRSupport
 * @details Reads DisplaySettings.getSettopHDRSupport
 *          (AIDL: IVideoDecoder / IHDMIOutput.getCapabilities) and validates that
 *          supportsHDR is consistent with the reported standards list.
 *
 * @precondition
 *  - org.rdk.DisplaySettings and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - Non-empty standards[]; supportsHDR true iff an HDR standard is listed.
 *
 * @pass_criteria
 *  - standards is a non-empty list, supportsHDR is boolean and consistent.
 *
 * @failure_criteria
 *  - Missing fields, empty list or inconsistent supportsHDR.
 */
"""

import time

from utils import finish, log_info
import VideoPort_Helpers as vh

TC = "TCID09_GetSettopHDRSupport"

NON_HDR = {"none", "sdr", ""}


def run_test():
    start = time.perf_counter()
    result = vh.get_settop_hdr_support()
    if result is None or not vh.result_success(result):
        return finish(TC, start, False, "getSettopHDRSupport failed")

    standards = result.get("standards")
    supports = result.get("supportsHDR")
    log_info(f"standards={standards}, supportsHDR={supports}")
    if not isinstance(standards, list) or not standards:
        return finish(TC, start, False, "standards[] missing or empty")
    if not isinstance(supports, bool):
        return finish(TC, start, False, "supportsHDR missing or not boolean")

    has_hdr = any(str(item).strip().lower() not in NON_HDR for item in standards)
    if has_hdr != supports:
        return finish(TC, start, False, f"supportsHDR={supports} inconsistent with standards {standards}")
    return finish(TC, start, True)
