"""
/**
 * @file SuiteManager.py
 * @brief SuiteManager.py
 *
 * @testcase SuiteManager
 * @details Orchestrates the VideoPort / VideoDevice (HDMI Output) L3 test suite by
 *          dynamically loading and executing test case modules, activating
 *          org.rdk.DeviceSettings and the client plugin stack (DisplaySettings,
 *          FrameRate, DisplayInfo, DeviceInfo, HdcpProfile) via JSON-RPC, and
 *          reporting per-test pass/fail/skip results with summary statistics.
 *
 * @precondition
 *  - WPEFramework is running and reachable at the configured JSON-RPC endpoint.
 *  - All plugins in SUITE_PLUGIN_CALLSIGNS are available for activation.
 *  - All test case modules listed in SUITES are present under the Testcases/ directory.
 *
 * @dependencies
 *  - utils.py
 *  - VideoPort_Curl.py
 *  - VideoPort_Helpers.py
 *  - Testcases/*.py
 *
 * @expected_result
 *  - All registered test cases are executed in order and results are logged.
 *
 * @pass_criteria
 *  - Each test case module's run_test() returns True (PASSED) or utils.SKIPPED
 *    (SKIPPED: precondition such as a connected display not met).
 *
 * @failure_criteria
 *  - Any test case returns False, raises an exception, or the plugin fails to activate.
 */
"""

import importlib
import io
import sys
import time
from pathlib import Path
import os

from utils import (
    activate_plugin,
    deactivate_plugin,
    log_error,
    log_info,
    log_success,
    log_warning,
    PLUGIN_INIT_WAIT,
    SKIPPED,
    WPEFRAMEWORK_JSONRPC_URL,
)


BASE_DIR = Path(__file__).resolve().parent
SUITES = {
    "videoport": {
        "banner": "*************** L3 SUITE - RDK - VIDEO PORT / VIDEO DEVICE (HDMI OUT) ***************",
        "module_dir": BASE_DIR / "Testcases",
        "tests": [
            # A. Read-only API validation
            "TCID01_DisplayConnected",
            "TCID02_GetCurrentResolution",
            "TCID03_GetHDCPStatus",
            "TCID04_SupportedHdcp",
            "TCID05_GetActiveInput",
            "TCID06_GetTVHDRCapabilities",
            "TCID07_GetSupportedTvResolutions",
            "TCID08_HdrSettingAndEotf",
            "TCID09_GetSettopHDRSupport",
            "TCID10_GetDisplayFrameRate",
            # B. Set API + state change + event capture
            "TCID11_SetCurrentResolution",
            "TCID12_SetSameResolutionNoEvent",
            "TCID13_SetDisplayFrameRate",
            "TCID14_ResolutionChangeTriggersFrameRateEvent",
            "TCID15_SetForceHDRMode",
            "TCID16_SetZoomSetting",
            "TCID17_SetFrmMode",
            # C. Scenarios / negative
            "TCID18_ResolutionCrossPluginConsistency",
            "TCID19_ResolutionPersistence",
            "TCID20_ZoomPersistence",
            "TCID21_NegativeAndBoundary",
        ],
    },
}

# Client plugins exercised by the suite (activated in this order, deactivated in reverse).
SUITE_PLUGIN_CALLSIGNS = {
    "videoport": [
        "org.rdk.DisplaySettings",
        "org.rdk.FrameRate",
        "DisplayInfo",
        "DeviceInfo",
        "org.rdk.HdcpProfile",
    ],
}

SUITE_PREREQUISITE_CALLSIGNS = {
    "videoport": ["org.rdk.DeviceSettings"],
}

SUITE_INIT_MODULES = {
    "videoport": "Init_VideoPort_Populate",
}


def normalize_suite_name(raw_name):
    return raw_name.strip().replace("_", "").replace("-", "").lower()


def normalize_test_name(raw_name):
    return raw_name.strip().replace("_", "").replace("-", "").lower()


def resolve_selected_tests(suite_name, requested_tests):
    if not requested_tests:
        return list(SUITES[suite_name]["tests"])

    available_tests = SUITES[suite_name]["tests"]
    resolved = []

    for requested in requested_tests:
        requested_norm = normalize_test_name(requested)
        matches = [name for name in available_tests if normalize_test_name(name) == requested_norm]
        if not matches:
            raise ValueError(f"Unknown test '{requested}'. Available tests: {available_tests}")
        resolved.extend(matches)

    return resolved


def load_test_cases(suite_name, selected_tests=None):
    suite_config = SUITES[suite_name]
    module_dir = str(suite_config["module_dir"])

    if module_dir not in sys.path:
        sys.path.insert(0, module_dir)

    test_cases = []
    for module_name in resolve_selected_tests(suite_name, selected_tests):
        module = importlib.import_module(module_name)
        run_fn = getattr(module, "run_test", None)
        if not callable(run_fn):
            module_file = getattr(module, "__file__", "<unknown>")
            exported = [name for name in dir(module) if not name.startswith("__")]
            raise AttributeError(
                f"Module '{module_name}' from '{module_file}' does not expose callable run_test(). "
                f"Exported names: {exported}"
            )
        test_cases.append((module_name, run_fn))

    return suite_config["banner"], test_cases


def run_suite_init(suite_name):
    module_name = SUITE_INIT_MODULES.get(suite_name)
    if not module_name:
        return True

    if BASE_DIR.as_posix() not in sys.path:
        sys.path.insert(0, str(BASE_DIR))

    try:
        module = importlib.import_module(module_name)
    except Exception as exc:
        log_error(f"Init module import failed: {module_name} ({exc})")
        return False

    run_fn = getattr(module, "run_test", None)
    if not callable(run_fn):
        log_error(f"Init module missing run_test(): {module_name}")
        return False

    log_info(f"Running suite initialization: {module_name}.run_test()")
    try:
        ok = bool(run_fn())
    except Exception as exc:
        log_error(f"Suite initialization threw exception: {exc}")
        return False

    if ok:
        log_success("Suite initialization completed successfully")
    else:
        log_error("Suite initialization failed")
    return ok


def activate_test_plugins(suite_name):
    callsigns = list(SUITE_PREREQUISITE_CALLSIGNS.get(suite_name, []))
    plugin_callsigns = SUITE_PLUGIN_CALLSIGNS.get(suite_name, [])
    if isinstance(plugin_callsigns, str):
        plugin_callsigns = [plugin_callsigns]
    callsigns.extend(plugin_callsigns)

    activated = []
    for callsign in callsigns:
        log_info(
            f"Auto-activating plugin '{callsign}' "
            f"via curl JSON-RPC at {WPEFRAMEWORK_JSONRPC_URL}"
        )
        if not activate_plugin(callsign):
            log_error(f"Plugin activation failed: {callsign}")
            return False, activated
        activated.append(callsign)
        log_success(f"Plugin activated: {callsign}")
        log_info(f"Waiting {PLUGIN_INIT_WAIT}s for plugin to fully initialise...")
        time.sleep(PLUGIN_INIT_WAIT)

    return True, activated


def deactivate_test_plugins(callsigns):
    success = True
    for callsign in reversed(callsigns):
        log_info(f"Auto-deactivating plugin '{callsign}'")
        if deactivate_plugin(callsign):
            log_success(f"Plugin deactivated: {callsign}")
        else:
            log_error(f"Plugin deactivation failed: {callsign}")
            success = False
    return success


def run_suite(suite_name, selected_tests=None):
    banner, test_cases = load_test_cases(suite_name, selected_tests)
    print(banner)

    auto_activate = os.environ.get("AUTO_ACTIVATE_PLUGINS", "1").lower() not in ("0", "false", "no")

    passed = 0
    failed = 0
    skipped = 0
    skipped_cases = []
    failed_cases = []
    original_stdout = sys.stdout

    for tc_name, tc_fn in test_cases:
        log_info(f"\n{'='*60}")
        log_info(f"Running: {tc_name}")
        log_info(f"{'='*60}")
        captured = io.StringIO()
        sys.stdout = captured
        activated_callsigns = []
        try:
            plugins_ready = True
            if auto_activate:
                plugins_ready, activated_callsigns = activate_test_plugins(suite_name)

            if not plugins_ready:
                log_error("Testcase setup failed because plugin activation did not complete.")
                result = False
            elif not run_suite_init(suite_name):
                log_error("Testcase setup failed because initialization did not complete.")
                result = False
            else:
                result = tc_fn()
        except Exception as exc:
            result = False
            print(f"EXCEPTION in {tc_name}: {exc}")
        finally:
            if auto_activate and not deactivate_test_plugins(activated_callsigns):
                if result != SKIPPED:
                    result = False
            sys.stdout = original_stdout

        output = captured.getvalue()
        print(output, end="")

        if result == SKIPPED:
            skipped += 1
            skipped_cases.append(tc_name)
            log_warning(f"[SKIP] {tc_name}")
        elif result is True:
            passed += 1
            log_success(f"[PASS] {tc_name}")
        else:
            failed += 1
            failed_cases.append(tc_name)
            log_error(f"[FAIL] {tc_name}")

        time.sleep(1)

    log_info(f"\n{'='*60}")
    log_info(f"Suite Summary: {passed} passed, {failed} failed, {skipped} skipped")
    if skipped_cases:
        log_warning(f"Skipped cases: {skipped_cases}")
    if failed_cases:
        log_error(f"Failed cases: {failed_cases}")
    log_info(f"{'='*60}")
    return failed == 0


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Run VideoPort / VideoDevice (HDMI Output) L3 test suites")
    parser.add_argument("suite", help=f"Test suite name. Available: {list(SUITES.keys())}")
    parser.add_argument("-t", "--timing", action="store_true", help="Enable timing output for passed test cases")
    parser.add_argument(
        "--test",
        dest="tests",
        action="append",
        help="Run only the named testcase module. Repeat to run multiple specific tests.",
    )

    args = parser.parse_args()

    if args.timing:
        os.environ["VIDEOPORT_TIMING_ENABLED"] = "1"

    suite_arg = normalize_suite_name(args.suite)
    matching = [k for k in SUITES if normalize_suite_name(k) == suite_arg]
    if not matching:
        log_error(f"Unknown suite '{args.suite}'. Available: {list(SUITES.keys())}")
        sys.exit(1)

    try:
        ok = run_suite(matching[0], args.tests)
    except (ValueError, AttributeError) as exc:
        log_error(str(exc))
        sys.exit(1)
    sys.exit(0 if ok else 1)
