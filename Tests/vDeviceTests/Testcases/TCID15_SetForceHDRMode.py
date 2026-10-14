"""
/**
 * @file TCID15_SetForceHDRMode.py
 * @brief L3 VideoPort set + state + event testcase.
 *
 * @testcase TCID15_SetForceHDRMode
 * @details Calls DisplaySettings.setForceHDRMode {hdr_mode:true} then {false}
 *          (AIDL: IHDMIOutputController.setProperty(HDR_OUTPUT_MODE)). When the TV
 *          supports HDR, asserts DisplaySettings.videoFormatChanged
 *          (AIDL: IPanelOutputListener.onVideoFormatChanged), hdrsetting != HdrOff
 *          and eotf PQ/HLG; then HdrOff after disabling. When the TV reports no
 *          HDR capability both calls must succeed and hdrsetting stays HdrOff.
 *
 * @note Known gap #4: IPanelOutputListener is not registered by the AIDL impl,
 *       so videoFormatChanged may not arrive.
 *
 * @precondition
 *  - Display connected (otherwise SKIPPED); plugin stack active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - HDR enabled/disabled as requested and videoFormatChanged received.
 *
 * @pass_criteria
 *  - Calls succeed, event received (HDR-capable TV), state as expected.
 *
 * @failure_criteria
 *  - Call rejected, event missing, or hdrsetting/eotf not as expected.
 */
"""

import time

from utils import SKIPPED, finish, log_warning
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID15_SetForceHDRMode"
HDR_BITS = 0x1F
HDR_EOTF = {"EOTF_SMPTE_ST_2084", "EOTF_BT2100"}


def _set(mode):
    _, result = vh.call(vc.curl_set_force_hdr_mode(mode), f"setForceHDRMode {mode}")
    return vh.result_success(result)


def run_test():
    start = time.perf_counter()
    if vh.get_connected() is not True:
        return finish(TC, start, SKIPPED, "display not connected")

    caps = vh.get_hdr_capabilities()
    if caps is None:
        return finish(TC, start, False, "getTVHDRCapabilities failed")
    baseline = vh.get_hdr_setting()

    if not caps & HDR_BITS:
        log_warning(f"TV reports no HDR capability (0x{caps:X}); verifying calls succeed and HDR stays off")
        if not _set(True) or not _set(False):
            return finish(TC, start, False, "setForceHDRMode rejected on SDR-only TV")
        time.sleep(1)
        hdr = vh.get_hdr_setting()
        if hdr != "HdrOff":
            return finish(TC, start, False, f"hdrsetting is {hdr} on SDR-only TV, expected HdrOff")
        return finish(TC, start, True)

    listener = vh.open_listener(TC, [vc.EVT_VIDEO_FORMAT_CHANGED])
    if listener is None:
        return finish(TC, start, False, "event listener registration failed")
    try:
        if not _set(True):
            return finish(TC, start, False, "setForceHDRMode true rejected")
        if vh.expect_event(listener, vc.EVT_VIDEO_FORMAT_CHANGED) is None:
            return finish(TC, start, False,
                          "videoFormatChanged not received (known gap #4: IPanelOutputListener not registered)")
        hdr = vh.get_hdr_setting()
        eotf = vh.get_eotf()
        if hdr in (None, "HdrOff"):
            return finish(TC, start, False, f"hdrsetting is {hdr} after forcing HDR")
        if eotf not in HDR_EOTF:
            return finish(TC, start, False, f"eotf is {eotf} after forcing HDR, expected one of {sorted(HDR_EOTF)}")

        if not _set(False):
            return finish(TC, start, False, "setForceHDRMode false rejected")
        vh.expect_event(listener, vc.EVT_VIDEO_FORMAT_CHANGED)
        hdr = vh.get_hdr_setting()
        if hdr != "HdrOff":
            return finish(TC, start, False, f"hdrsetting is {hdr} after disabling forced HDR")
        return finish(TC, start, True)
    finally:
        listener.close()
        if baseline == "HdrOff":
            _set(False)
