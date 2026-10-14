"""
/**
 * @file TCID19_ResolutionPersistence.py
 * @brief L3 VideoPort persistence scenario.
 *
 * @testcase TCID19_ResolutionPersistence
 * @details Sets a resolution with persist:true (720p, or 1080p if already 720p),
 *          restarts org.rdk.DeviceSettings, org.rdk.DisplaySettings and
 *          org.rdk.FrameRate, and verifies getCurrentResolution still returns the
 *          persisted value (dVideoPortAIDLImpl persists HDMI0.resolution).
 *
 * @precondition
 *  - Display connected (otherwise SKIPPED); plugin stack active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - The persisted resolution survives the plugin restart.
 *
 * @pass_criteria
 *  - Set succeeds and getter returns the target before and after restart.
 *
 * @failure_criteria
 *  - Set rejected, restart failed, or resolution not persisted.
 */
"""

import time

from utils import SKIPPED, finish, restart_plugins
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID19_ResolutionPersistence"


def run_test():
    start = time.perf_counter()
    if vh.get_connected() is not True:
        return finish(TC, start, SKIPPED, "display not connected; setCurrentResolution requires HPD")

    baseline = vh.get_resolution_name()
    target = "1080p" if baseline == "720p" else "720p"
    try:
        _, result = vh.call(vc.curl_set_current_resolution(target, persist=True),
                            f"setCurrentResolution {target} persist")
        if not vh.result_success(result):
            return finish(TC, start, False, f"setCurrentResolution {target} rejected")
        time.sleep(2)
        if vh.get_resolution_name() != target:
            return finish(TC, start, False, f"resolution not applied before restart (expected {target})")

        if not restart_plugins(vc.RESTART_STACK):
            return finish(TC, start, False, "plugin restart failed")

        after = vh.get_resolution_name()
        if after != target:
            return finish(TC, start, False, f"after restart resolution is {after}, expected persisted {target}")
        return finish(TC, start, True)
    finally:
        vh.restore_resolution(baseline)
