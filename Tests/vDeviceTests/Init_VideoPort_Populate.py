"""
/**
 * @file Init_VideoPort_Populate.py
 * @brief Suite initialization / precondition for the VideoPort L3 suite.
 *
 * @testcase Init_VideoPort_Populate
 * @details Verifies the plugin stack is reachable and logs the baseline display
 *          state (connection, resolution, frame rate, HDR, zoom, FRM mode) so
 *          that every testcase log carries the starting conditions.
 *
 * @precondition
 *  - org.rdk.DeviceSettings and the client plugin stack are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py, SuiteManager.py
 *
 * @expected_result
 *  - DisplayInfo.connected and getCurrentResolution respond with valid results.
 *
 * @pass_criteria
 *  - Both baseline getters return valid values; run_test() returns True.
 *
 * @failure_criteria
 *  - Endpoint unreachable or either baseline getter fails.
 */
"""

from utils import log_info, log_success, log_error
import VideoPort_Helpers as vh


def run_test():
    log_info("VideoPort suite initialization: verifying plugin reachability")

    connected = vh.get_connected()
    if connected is None:
        log_error("Init failed: DisplayInfo.connected did not return a boolean")
        return False

    resolution = vh.get_current_resolution()
    if resolution is None:
        log_error("Init failed: getCurrentResolution did not return a resolution")
        return False

    log_info("Baseline display state:")
    log_info(f"  connected           : {connected}")
    log_info(f"  resolution          : {resolution.get('resolution')}")
    log_info(f"  displayFrameRate    : {vh.get_display_framerate_raw()}")
    log_info(f"  hdrsetting          : {vh.get_hdr_setting()}")
    log_info(f"  zoomSetting         : {vh.get_zoom_setting()}")
    log_info(f"  auto-frm-mode       : {vh.get_frm_mode()}")

    log_success("Suite initialization completed successfully")
    return True
