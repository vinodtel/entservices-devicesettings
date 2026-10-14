"""
/**
 * @file TCID05_GetActiveInput.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID05_GetActiveInput
 * @details Reads DisplaySettings.getActiveInput {videoDisplay: HDMI0}. Not listed
 *          in the mapping PDF; the AIDL implementation maps it to the HPD state.
 *
 * @precondition
 *  - org.rdk.DisplaySettings, DisplayInfo and org.rdk.DeviceSettings are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - activeInput is a boolean; false when the display is disconnected.
 *
 * @pass_criteria
 *  - Boolean result and not true while disconnected.
 *
 * @failure_criteria
 *  - Missing/non-boolean activeInput, or activeInput true while disconnected.
 */
"""

import time

from utils import finish, log_info, log_warning
import VideoPort_Helpers as vh

TC = "TCID05_GetActiveInput"


def run_test():
    start = time.perf_counter()
    active = vh.get_active_input()
    if active is None:
        return finish(TC, start, False, "getActiveInput did not return a boolean activeInput")
    connected = vh.get_connected()
    log_info(f"activeInput={active}, connected={connected}")

    if connected is False and active:
        return finish(TC, start, False, "activeInput is true while the display is disconnected")
    if connected is True and not active:
        log_warning("Display connected but activeInput is false (sink may be in standby)")
    return finish(TC, start, True)
