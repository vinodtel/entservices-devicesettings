"""
/**
 * @file TCID03_GetHDCPStatus.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID03_GetHDCPStatus
 * @details Reads org.rdk.HdcpProfile.getHDCPStatus (AIDL: getHDCPStatus,
 *          getHDCPReceiverVersion, getHDCPCurrentVersion) and cross-checks
 *          isConnected against DisplayInfo.connected (HPD).
 *
 * @precondition
 *  - org.rdk.HdcpProfile, DisplayInfo and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - HDCPStatus object contains all fields; isConnected == DisplayInfo.connected.
 *
 * @pass_criteria
 *  - All required fields present and connection state consistent.
 *
 * @failure_criteria
 *  - Missing HDCPStatus/fields or inconsistent isConnected.
 */
"""

import time

from utils import finish, log_info
import VideoPort_Helpers as vh

TC = "TCID03_GetHDCPStatus"

REQUIRED_FIELDS = [
    "isConnected",
    "isHDCPCompliant",
    "isHDCPEnabled",
    "hdcpReason",
    "supportedHDCPVersion",
    "receiverHDCPVersion",
    "currentHDCPVersion",
]


def run_test():
    start = time.perf_counter()
    status = vh.get_hdcp_status()
    if status is None:
        return finish(TC, start, False, "getHDCPStatus did not return an HDCPStatus object")

    missing = [field for field in REQUIRED_FIELDS if field not in status]
    if missing:
        return finish(TC, start, False, f"HDCPStatus missing fields: {missing}")
    log_info(f"HDCPStatus: {status}")

    connected = vh.get_connected()
    if connected is None:
        return finish(TC, start, False, "DisplayInfo.connected did not return a boolean")
    if bool(status["isConnected"]) != connected:
        return finish(TC, start, False,
                      f"HDCPStatus.isConnected={status['isConnected']} but DisplayInfo.connected={connected}")
    return finish(TC, start, True)
