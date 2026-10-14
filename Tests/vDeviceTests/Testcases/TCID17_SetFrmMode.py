"""
/**
 * @file TCID17_SetFrmMode.py
 * @brief L3 VideoPort set + state testcase.
 *
 * @testcase TCID17_SetFrmMode
 * @details Sets org.rdk.FrameRate.setFrmMode 1 then 0
 *          (AIDL: IPanelOutput.setFrameRateMatching, no listener) and verifies each
 *          value via getFrmMode.auto-frm-mode.
 *
 * @note Known gap #1: SetFRFMode/GetFRFMode return ERROR_UNAVAILABLE.
 *
 * @precondition
 *  - org.rdk.FrameRate and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - getFrmMode returns 1, then 0.
 *
 * @pass_criteria
 *  - Both sets succeed and the getter reflects each value.
 *
 * @failure_criteria
 *  - Getter unavailable, set rejected or getter mismatch.
 */
"""

import time

from utils import finish
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID17_SetFrmMode"
GAP = "known gap #1: SetFRFMode/GetFRFMode return ERROR_UNAVAILABLE"


def run_test():
    start = time.perf_counter()
    baseline = vh.get_frm_mode()
    if baseline is None:
        return finish(TC, start, False, f"getFrmMode failed ({GAP})")

    try:
        for value in (vc.FRM_MODE_ENABLED, vc.FRM_MODE_DISABLED):
            _, result = vh.call(vc.curl_set_frm_mode(value), f"setFrmMode {value}")
            if not vh.result_success(result):
                return finish(TC, start, False, f"setFrmMode {value} rejected ({GAP})")
            current = vh.get_frm_mode()
            if current != value:
                return finish(TC, start, False, f"getFrmMode returned {current}, expected {value}")
        return finish(TC, start, True)
    finally:
        if vh.get_frm_mode() != baseline:
            vh.call(vc.curl_set_frm_mode(baseline), f"Restore frm mode {baseline}")
