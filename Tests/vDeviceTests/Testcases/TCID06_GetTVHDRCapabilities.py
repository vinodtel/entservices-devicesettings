"""
/**
 * @file TCID06_GetTVHDRCapabilities.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID06_GetTVHDRCapabilities
 * @details Reads DisplaySettings.getTVHDRCapabilities (AIDL: EDID via
 *          IHDMIOutputControllerListener.onEDID) and validates the bitmask
 *          (HDR10 0x1, HLG 0x2, DV 0x4, Technicolor 0x8, HDR10+ 0x10, SDR 0x20).
 *
 * @precondition
 *  - org.rdk.DisplaySettings and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - capabilities is an integer bitmask within 0x3F.
 *
 * @pass_criteria
 *  - Integer, non-negative, no bits outside 0x3F.
 *
 * @failure_criteria
 *  - Missing/non-integer capabilities or unknown bits set.
 */
"""

import time

from utils import finish, log_info
import VideoPort_Helpers as vh

TC = "TCID06_GetTVHDRCapabilities"


def run_test():
    start = time.perf_counter()
    caps = vh.get_hdr_capabilities()
    if caps is None:
        return finish(TC, start, False, "getTVHDRCapabilities did not return an integer bitmask")
    if caps < 0 or caps & ~vh.HDR_CAP_MASK:
        return finish(TC, start, False, f"capabilities 0x{caps:X} has bits outside 0x{vh.HDR_CAP_MASK:X}")
    log_info(f"TV HDR capabilities 0x{caps:X}: {vh.decode_hdr_caps(caps) or ['none']}")
    return finish(TC, start, True)
