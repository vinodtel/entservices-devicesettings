"""
/**
 * @file TCID07_GetSupportedTvResolutions.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID07_GetSupportedTvResolutions
 * @details Reads DisplaySettings.getSupportedTvResolutions {videoDisplay: HDMI0}
 *          (AIDL: EDID via onEDID) and validates the list against the connection
 *          state and the current resolution.
 *
 * @precondition
 *  - org.rdk.DisplaySettings, DisplayInfo and org.rdk.DeviceSettings are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - A list of resolution strings, non-empty when a display is connected.
 *
 * @pass_criteria
 *  - List of strings; non-empty when connected. Current resolution missing
 *    from the list is reported as a warning only.
 *
 * @failure_criteria
 *  - Missing list, non-string entries, or empty list while connected.
 */
"""

import time

from utils import finish, log_info, log_warning
import VideoPort_Helpers as vh

TC = "TCID07_GetSupportedTvResolutions"


def run_test():
    start = time.perf_counter()
    resolutions = vh.get_supported_tv_resolutions()
    if resolutions is None:
        return finish(TC, start, False, "getSupportedTvResolutions did not return a list")
    if not all(isinstance(item, str) for item in resolutions):
        return finish(TC, start, False, f"Non-string entries in supportedTvResolutions: {resolutions}")
    log_info(f"supportedTvResolutions ({len(resolutions)}): {resolutions}")

    connected = vh.get_connected()
    if connected and not resolutions:
        return finish(TC, start, False, "Display connected but supportedTvResolutions is empty")

    current = vh.get_resolution_name()
    if connected and current and current not in resolutions:
        log_warning(f"Current resolution {current} not present in supportedTvResolutions")
    return finish(TC, start, True)
