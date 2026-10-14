"""
/**
 * @file TCID20_ZoomPersistence.py
 * @brief L3 VideoPort persistence scenario.
 *
 * @testcase TCID20_ZoomPersistence
 * @details Sets the zoom setting (NONE, or FULL if already NONE), restarts
 *          org.rdk.DeviceSettings, org.rdk.DisplaySettings and org.rdk.FrameRate,
 *          and verifies getZoomSetting returns the persisted value
 *          (dVideoDeviceAIDLImpl persists VideoDevice.DFC).
 *
 * @precondition
 *  - org.rdk.DisplaySettings and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - The zoom setting survives the plugin restart.
 *
 * @pass_criteria
 *  - Set succeeds and getter returns the target before and after restart.
 *
 * @failure_criteria
 *  - Set rejected, restart failed, or value not persisted.
 */
"""

import time

from utils import finish, restart_plugins
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID20_ZoomPersistence"


def run_test():
    start = time.perf_counter()
    baseline = vh.get_zoom_setting()
    if baseline is None:
        return finish(TC, start, False, "getZoomSetting failed")
    target = vc.ZOOM_FULL if baseline == vc.ZOOM_NONE else vc.ZOOM_NONE
    try:
        _, result = vh.call(vc.curl_set_zoom_setting(target), f"setZoomSetting {target}")
        if not vh.result_success(result):
            return finish(TC, start, False, f"setZoomSetting {target} rejected")
        if vh.get_zoom_setting() != target:
            return finish(TC, start, False, f"zoom not applied before restart (expected {target})")

        if not restart_plugins(vc.RESTART_STACK):
            return finish(TC, start, False, "plugin restart failed")

        after = vh.get_zoom_setting()
        if after != target:
            return finish(TC, start, False, f"after restart zoom is {after}, expected persisted {target}")
        return finish(TC, start, True)
    finally:
        if vh.get_zoom_setting() != baseline:
            vh.call(vc.curl_set_zoom_setting(baseline), f"Restore zoom {baseline}")
