"""
/**
 * @file TCID16_SetZoomSetting.py
 * @brief L3 VideoPort set + state testcase.
 *
 * @testcase TCID16_SetZoomSetting
 * @details Sets DisplaySettings.setZoomSetting NONE then FULL
 *          (AIDL: IPlaneControl.setProperty(ASPECT_RATIO)) and verifies each value via
 *          DisplaySettings.1.getZoomSetting. IPlaneControl has no listener in the
 *          mapping, so zoomSettingUpdated is logged only.
 *
 * @precondition
 *  - org.rdk.DisplaySettings and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - getZoomSetting returns NONE, then FULL.
 *
 * @pass_criteria
 *  - Both sets succeed and the getter reflects each value.
 *
 * @failure_criteria
 *  - Set rejected or getter mismatch.
 */
"""

import time

from utils import finish
import VideoPort_Curl as vc
import VideoPort_Helpers as vh

TC = "TCID16_SetZoomSetting"


def run_test():
    start = time.perf_counter()
    baseline = vh.get_zoom_setting()
    if baseline is None:
        return finish(TC, start, False, "getZoomSetting failed")

    listener = vh.open_listener(TC, [vc.EVT_ZOOM_UPDATED])
    try:
        for value in (vc.ZOOM_NONE, vc.ZOOM_FULL):
            _, result = vh.call(vc.curl_set_zoom_setting(value), f"setZoomSetting {value}")
            if not vh.result_success(result):
                return finish(TC, start, False, f"setZoomSetting {value} rejected")
            current = vh.get_zoom_setting()
            if current != value:
                return finish(TC, start, False, f"getZoomSetting returned {current}, expected {value}")
        if listener:
            listener.collect(1)
            vh.log_informational_events(listener, [vc.EVT_ZOOM_UPDATED])
        return finish(TC, start, True)
    finally:
        if listener:
            listener.close()
        if vh.get_zoom_setting() != baseline:
            vh.call(vc.curl_set_zoom_setting(baseline), f"Restore zoom {baseline}")
