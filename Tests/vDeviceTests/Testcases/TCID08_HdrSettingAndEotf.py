"""
/**
 * @file TCID08_HdrSettingAndEotf.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID08_HdrSettingAndEotf
 * @details Reads DisplayInfo.hdrsetting and DisplayInfo.eotf
 *          (AIDL: IHDMIOutput.getProperty(HDR_OUTPUT_MODE)), validates both enums
 *          and checks the two values are mutually consistent.
 *
 * @precondition
 *  - DisplayInfo and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - Valid enums; HdrOff => BT1886/UNKNOWN/OTHER, Hdr10/Hdr10Plus => ST_2084,
 *    HdrHlg => BT2100.
 *
 * @pass_criteria
 *  - Both values are valid enums and consistent.
 *
 * @failure_criteria
 *  - Missing/unknown value or inconsistent combination.
 */
"""

import time

from utils import finish, log_info
import VideoPort_Helpers as vh

TC = "TCID08_HdrSettingAndEotf"

CONSISTENT_EOTF = {
    "HdrOff": {"EOTF_BT1886", "EOTF_UNKNOWN", "EOTF_OTHER"},
    "Hdr10": {"EOTF_SMPTE_ST_2084"},
    "Hdr10Plus": {"EOTF_SMPTE_ST_2084"},
    "HdrHlg": {"EOTF_BT2100"},
}


def run_test():
    start = time.perf_counter()
    hdr = vh.get_hdr_setting()
    eotf = vh.get_eotf()
    log_info(f"hdrsetting={hdr}, eotf={eotf}")

    if hdr not in vh.HDR_SETTINGS:
        return finish(TC, start, False, f"hdrsetting '{hdr}' not in {sorted(vh.HDR_SETTINGS)}")
    if eotf not in vh.EOTF_VALUES:
        return finish(TC, start, False, f"eotf '{eotf}' not in {sorted(vh.EOTF_VALUES)}")

    allowed = CONSISTENT_EOTF.get(hdr)
    if allowed is not None and eotf not in allowed:
        return finish(TC, start, False, f"eotf {eotf} inconsistent with hdrsetting {hdr} (expected {sorted(allowed)})")
    return finish(TC, start, True)
