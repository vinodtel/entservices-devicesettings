# VideoPort / VideoDevice (HDMI Output) – L3 Test Scenarios (DRAFT v2 FOR REVIEW)

Scope: **curl/JSON-RPC driven only** (no vComponent injection). Each *set* API is followed by
(a) a *get* to verify the state change, and (b) an event check **only where the mapping PDF lists an
AIDL listener** for that path.

Framework: `vDeviceTests` style (`SuiteManager.py`, `utils.send_jsonrpc_command`, `utils.JsonRpcEventListener`).
Proposed suite: `videoport`. Plugins activated: `org.rdk.DeviceSettings` → `org.rdk.DisplaySettings`,
`org.rdk.FrameRate`, `DisplayInfo`, `DeviceInfo`, `org.rdk.HdcpProfile`.
Every state-changing test captures a baseline and restores it in `finally`.

## AIDL event listeners available (per mapping PDF)

| AIDL listener | Legacy event | JSON-RPC event asserted |
|---|---|---|
| `IHDMIOutputControllerListener.onFrameRateChanged()` | `DSMGR_EVENT_DISPLAY_FRAMRATE_POSTCHANGE` | `FrameRate.onDisplayFrameRateChanged`, `DisplaySettings.resolutionChanged` (VideoPort `OnFrameRateChanged` → `OnResolutionPostChange`) |
| `IHDMIOutputControllerListener.onHDCPStatusChanged(status, version)` | `DSMGR_EVENT_HDCP_STATUS` | No curl API triggers it, so it is only verified passively via `HdcpProfile.getHDCPStatus` |
| `IPanelOutputListener.onVideoFormatChanged()` | `DSMGR_EVENT_VIDEO_FORMAT_UPDATE` | `DisplaySettings.videoFormatChanged` |
| *(none: PRECHANGE is "NA / Deprecated")* | `DSMGR_EVENT_DISPLAY_FRAMRATE_PRECHANGE` | `onDisplayFrameRateChanging` / `resolutionPreChange` are **logged only, not asserted** |
| *(none: IPlaneControl / IPanelOutput.setFrameRateMatching have no listener)* | – | `zoomSettingUpdated`: **state verified via getter only** |

---

## A. Read-only API tests (state snapshot)

| TCID | Name | Curl API | AIDL mapping (PDF) | Pass criteria |
|---|---|---|---|---|
| 01 | DisplayConnected | `DisplayInfo.connected` | `IHDMIOutputController.getHotPlugDetectState()` | Boolean result, stored to gate connected/disconnected expectations. |
| 02 | GetCurrentResolution | `DisplaySettings.getCurrentResolution {HDMI0}` | `IHDMIOutput.getProperty(VIC)` | `success:true`; resolution is in the VIC table. |
| 03 | HDCPStatus | `HdcpProfile.getHDCPStatus` | `getHDCPStatus()`, `getHDCPReceiverVersion()`, `getHDCPCurrentVersion()` | All `HDCPStatus` fields are present and `isConnected` equals `DisplayInfo.connected`. |
| 04 | SupportedHdcp | `DeviceInfo.supportedhdcp {HDMI0}` | `IHDMIOutput.getCapabilities().supportedHDCPProtocolVersions` | Value is a valid HDCP version and consistent with `HdcpProfile.supportedHDCPVersion`. |
| 05 | ActiveInput | `DisplaySettings.getActiveInput {HDMI0}` | NA in PDF (impl maps to HPD) | `success:true`; `activeInput` is a boolean. |
| 06 | TVHDRCapabilities | `DisplaySettings.getTVHDRCapabilities` | `IHDMIOutputControllerListener.onEDID()` (EDID) | `capabilities` bitmask is consistent with `standards[]`. |
| 07 | SupportedTvResolutions | `DisplaySettings.getSupportedTvResolutions {HDMI0}` | `onEDID()` (EDID) | Non-empty list when connected. |
| 08 | HdrSettingAndEotf | `DisplayInfo.hdrsetting`, `DisplayInfo.eotf` | `IHDMIOutput.getProperty(HDR_OUTPUT_MODE)` | Each value is in its enum and the two are mutually consistent. |
| 09 | SettopHDRSupport | `DisplaySettings.getSettopHDRSupport` | `IVideoDecoder/IHDMIOutput.getCapabilities()` | `standards[]` is non-empty and `supportsHDR` is consistent with it. |
| 10 | GetDisplayFrameRate | `FrameRate.getDisplayFrameRate` | `IPanelOutput.getRefreshRate` (+ `getVideoResolution`) | `framerate` matches `WxHpxR` and agrees with `getCurrentResolution`. |

## B. Set API + state change + AIDL-backed event

| TCID | Name | Set API (curl) | AIDL mapping | State check (get) | Event (listener) |
|---|---|---|---|---|---|
| 11 | SetCurrentResolution | `setCurrentResolution {HDMI0, 1080p, persist:true, ignoreEdid:true}`. If already 1080p, first move to 720p, because the same VIC skips the HAL. | `IHDMIOutputController.setProperty(VIC)` | `getCurrentResolution` == `1080p`; `DisplayInfo.connected` unchanged | **Assert** `resolutionChanged {width:1920,height:1080,videoDisplayType:HDMI0,resolution:1080p}` (`onFrameRateChanged` → post-change). Log `resolutionPreChange` only. |
| 12 | SetResolutionSameValue | Call `setCurrentResolution` with the current value | VIC already active, so no `setProperty` | Resolution unchanged | **Assert no** `resolutionChanged` within the timeout. |
| 13 | SetDisplayFrameRate | `FrameRate.setDisplayFrameRate {"3840x2160px48"}`, or a rate valid for the current family, e.g. `1920x1080px50` | `IPanelOutput.setRefreshRate` | `getDisplayFrameRate` reflects the requested rate | **Assert** `onDisplayFrameRateChanged {displayFrameRate}` (`onFrameRateChanged`). Log `onDisplayFrameRateChanging` only (PRECHANGE deprecated). |
| 14 | ResolutionChangeTriggersFrameRateEvent | `setCurrentResolution` across rates (e.g. `1080p60` → `1080p50`) | `setProperty(VIC)` → `onFrameRateChanged` | `getDisplayFrameRate` matches the new resolution | **Assert** both `resolutionChanged` and `FrameRate.onDisplayFrameRateChanged` from one AIDL callback. |
| 15 | SetForceHDRMode | `setForceHDRMode {hdr_mode:true}`, then `{false}` | `IHDMIOutputController.setProperty(HDR_OUTPUT_MODE)` | true → `hdrsetting`≠`HdrOff`, `eotf` = PQ/HLG (skipped if `getTVHDRCapabilities`=0). false → `HdrOff`. | **Assert** `videoFormatChanged {currentVideoFormat}` (`IPanelOutputListener.onVideoFormatChanged`). |
| 16 | SetZoomSetting | `setZoomSetting {NONE}`, then `{FULL}` | `IPlaneControl.setProperty(ASPECT_RATIO)` (no listener) | `DisplaySettings.1.getZoomSetting` returns NONE, then FULL | None asserted; `zoomSettingUpdated` is logged if received. |
| 17 | SetFrmMode | `setFrmMode {1}`, then `{0}` | `IPanelOutput.setFrameRateMatching` (no listener) | `getFrmMode.auto-frm-mode` returns 1, then 0 | None. |

## C. Scenario / persistence / negative (curl only)

| TCID | Name | Steps | Pass criteria |
|---|---|---|---|
| 18 | ResolutionCrossPluginConsistency | `setCurrentResolution` 720p → 1080p | After each `resolutionChanged`, `getCurrentResolution`, the WxH of `getDisplayFrameRate` and `getSupportedTvResolutions` membership all agree. |
| 19 | ResolutionPersistence | `setCurrentResolution 720p persist:true` → deactivate/activate `DeviceSettings` + `DisplaySettings` → get | Returns `720p`. |
| 20 | ZoomPersistence | `setZoomSetting NONE` → plugin restart → get | Returns `NONE`. |
| 21 | NegativeAndBoundary | Invalid `videoDisplay:"HDMI9"`, `resolution:"999p"`, `setFrmMode 5`, `setZoomSetting "BOGUS"`, `setDisplayFrameRate ""`/`"abc"`, `setForceHDRMode` without params | Each call returns an error or `success:false`, **no** post-change events arrive, and the state is unchanged per the getters. |

---

### Implementation gaps in current AIDL code that will cause expected failures
1. `SetFRFMode`/`GetFRFMode` → `ERROR_UNAVAILABLE` (TC17).
2. `SetDisplayFrameRate` calls `atoi("3840x2160px48")`, which yields 3840, so it gets BAD_REQUEST and no event fires (TC13).
3. `GetCurrentDisplayFrameRate` returns `"60"` instead of `WxHpxR` (TC10, TC13, TC18).
4. `IPanelOutputListener.onVideoFormatChanged` is not registered (only `IHDMIOutput` listeners are), so TC15's event may not arrive.
5. Zoom is cache/persistence only because `IPlaneControl` is not wired (TC16).
