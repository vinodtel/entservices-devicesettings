/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

/**
 * @file DeviceSettingsHALConfig.h
 * @brief Shared HAL configuration loading for DeviceSettings components.
 *
 * FPD, Audio, and VideoPort all use the same dlopen/dlsym/dlclose pattern to
 * read static configuration tables from the HAL shared library at startup.
 * This header centralises the public interface for those loaders so the three
 * implementation files do not each carry a private copy of the same code.
 *
 * Include this header AFTER Module.h (or after any header that includes
 * Module.h) in a DeviceSettings plugin compilation unit.
 */

#include "DeviceSettingsTypes.h"
#include <vector>

namespace DeviceSettingsHAL {

    // ─── Front Panel Display ───────────────────────────────────────────────────

    /**
     * Load FPD configuration from the HAL shared library and populate the
     * supplied vectors with deep-copied, heap-owned data.  The HAL library
     * handle is closed internally once the copy is complete.
     */
    void PopulateFPDConfig(
        std::vector<FPDColorConfig>& colors,
        std::vector<FPDIndicatorConfig>& indicators,
        std::vector<FPDTextDisplayConfig>& textDisplays,
        std::vector<FPDColorBinding>& colorBindings);

    /**
     * Log a human-readable dump of the FPD config vectors.
     * Gated by /opt/dsMgrDumpDeviceConfigs — no-op when that file is absent.
     */
    void DumpFPDConfig(
        const std::vector<FPDColorConfig>& colors,
        const std::vector<FPDIndicatorConfig>& indicators,
        const std::vector<FPDTextDisplayConfig>& textDisplays,
        const std::vector<FPDColorBinding>& colorBindings);

    // ─── Audio ─────────────────────────────────────────────────────────────────

    void PopulateAudioConfig(
        std::vector<AudioTypeConfigInfo>& audioTypes,
        std::vector<AudioPortConfigInfo>& audioPorts);

    void DumpAudioConfig(
        const std::vector<AudioTypeConfigInfo>& audioTypes,
        const std::vector<AudioPortConfigInfo>& audioPorts);

    // ─── Video Port ────────────────────────────────────────────────────────────

    void PopulateVideoPortConfig(
        std::vector<VideoPortTypeConfig>& videoPortTypes,
        std::vector<VideoPortPortConfig>& videoPorts);

    void PopulateVideoPortResolutionConfig(
        const VideoPortType videoPortType,
        std::vector<VideoPortResolution>& resolutions);

    void DumpVideoPortConfig(
        const std::vector<VideoPortTypeConfig>& videoPortTypes,
        const std::vector<VideoPortPortConfig>& videoPorts,
        const std::vector<VideoPortResolution>& resolutions);

    // ─── Video Device ──────────────────────────────────────────────────────────

    void PopulateVideoDeviceConfig(
        std::vector<VideoDeviceConfigInfo>& videoDeviceConfigs);

    void DumpVideoDeviceConfig(
        const std::vector<VideoDeviceConfigInfo>& videoDeviceConfigs);

#ifdef ENABLE_HDMIOUTPUT_AIDL
    // ─── HDMI Output AIDL fallback (implemented in hal/dVideoPortAIDLImpl.cpp) ──
    // Used when the DS HAL config tables are absent. Built from IHDMIOutput.getCapabilities().

    bool PopulateVideoPortConfigFromAIDL(
        std::vector<VideoPortTypeConfig>& videoPortTypes,
        std::vector<VideoPortPortConfig>& videoPorts);

    bool PopulateVideoPortResolutionConfigFromAIDL(
        const VideoPortType videoPortType,
        std::vector<VideoPortResolution>& resolutions);

    bool PopulateVideoDeviceConfigFromAIDL(
        std::vector<VideoDeviceConfigInfo>& videoDeviceConfigs);
#endif

} // namespace DeviceSettingsHAL
