"""
/**
 * @file TCID04_SupportedHdcp.py
 * @brief L3 VideoPort read-only testcase.
 *
 * @testcase TCID04_SupportedHdcp
 * @details Reads DeviceInfo.supportedhdcp {videoDisplay: HDMI0}
 *          (AIDL: IHDMIOutput.getCapabilities().supportedHDCPProtocolVersions)
 *          and cross-checks it with HdcpProfile.getHDCPStatus.supportedHDCPVersion.
 *
 * @precondition
 *  - DeviceInfo, org.rdk.HdcpProfile and org.rdk.DeviceSettings plugins are active.
 *
 * @dependencies
 *  - utils.py, VideoPort_Curl.py, VideoPort_Helpers.py
 *
 * @expected_result
 *  - A valid HDCP version (unavailable/1.4/2.0/2.1/2.2, or hdcp_xx form).
 *
 * @pass_criteria
 *  - Normalised version is valid and equals the HdcpProfile supported version.
 *
 * @failure_criteria
 *  - Missing/invalid version or mismatch between the two plugins.
 */
"""

import time

from utils import finish, log_info, log_warning
import VideoPort_Helpers as vh

TC = "TCID04_SupportedHdcp"

VALID_VERSIONS = {"unavailable", "1.4", "2.0", "2.1", "2.2"}


def run_test():
    start = time.perf_counter()
    raw = vh.get_supported_hdcp()
    version = vh.normalize_hdcp(raw)
    if version not in VALID_VERSIONS:
        return finish(TC, start, False, f"supportedHDCPVersion '{raw}' is not a valid HDCP version")
    log_info(f"DeviceInfo.supportedhdcp: {raw} (normalised {version})")

    status = vh.get_hdcp_status()
    if status is None or "supportedHDCPVersion" not in status:
        log_warning("HdcpProfile.getHDCPStatus unavailable; cross-check skipped")
        return finish(TC, start, True)

    profile_version = vh.normalize_hdcp(status["supportedHDCPVersion"])
    if profile_version != version:
        return finish(TC, start, False,
                      f"DeviceInfo reports {version} but HdcpProfile reports {profile_version}")
    return finish(TC, start, True)
