"""
/**
 * @file TCID01_DisplayConnected.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID01_DisplayConnected
 * @details Reads DisplayInfo.connected (AIDL: IHDMIOutputController.getHotPlugDetectState)
 *          and validates it is a boolean. The value gates connected/disconnected
 *          expectations of the other testcases.
 *
 * @precondition
 *  - DisplayInfo and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - DisplayInfo.connected returns true or false.
 *
 * @pass_criteria
 *  - Result is a boolean.
 *
 * @failure_criteria
 *  - No response, JSON-RPC error, or non-boolean result.
 */
"""

import time

from utils import finish, log_info
import VideoPort_Helpers as vh

TC = "TCID01_DisplayConnected"


def run_test():
    start = time.perf_counter()
    connected = vh.get_connected()
    if connected is None:
        return finish(TC, start, False, "DisplayInfo.connected did not return a boolean")
    log_info(f"Display connected (HPD): {connected}")
    return finish(TC, start, True)
