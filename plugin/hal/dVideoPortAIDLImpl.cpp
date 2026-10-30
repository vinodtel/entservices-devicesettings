/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
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
#include "dVideoPortAIDLImpl.h"
#include "DeviceSettingsTypes.h"
#include "DeviceSettingsHALConfig.h"
#include "DeviceSettingsHdmiStatus.h"
#include <dsDisplay.h>

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/PropertyValue.h>
#include <com/rdk/hal/hdmioutput/Capabilities.h>
#include <com/rdk/hal/hdmioutput/HDCPProtocolVersion.h>
#include <com/rdk/hal/hdmioutput/HDCPStatus.h>
#include <com/rdk/hal/hdmioutput/HDROutputMode.h>
#include <com/rdk/hal/hdmioutput/PixelFormat.h>
#include <com/rdk/hal/hdmioutput/Property.h>
#include <com/rdk/hal/hdmioutput/PropertyKVPair.h>
#include <com/rdk/hal/hdmioutput/State.h>
#include <com/rdk/hal/hdmioutput/VIC.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace HdmiOutput = com::rdk::hal::hdmioutput;
using PropertyValue = com::rdk::hal::PropertyValue;

namespace {
// Subset of VICs mapped to the resolution names understood by the rest of the plugin.
// Matches the resolution families produced by dVideoPortImpl::buildResolutionFromName().
struct VicResolutionEntry {
    HdmiOutput::VIC vic;
    const char* name;
    VideoResolution pixelResolution;
    VideoAspectRatio aspectRatio;
    VideoFrameRate frameRate;
    bool interlaced;
};

const VicResolutionEntry kVicTable[] = {
    { HdmiOutput::VIC::VIC3_720_480_P_60_16_9,     "480p",      VideoResolution::DS_VIDEO_PIXELRES_720X480,   VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, false },
    { HdmiOutput::VIC::VIC18_720_576_P_50_16_9,    "576p50",    VideoResolution::DS_VIDEO_PIXELRES_720X576,   VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, false },
    { HdmiOutput::VIC::VIC4_1280_720_P_60_16_9,    "720p",      VideoResolution::DS_VIDEO_PIXELRES_1280X720,  VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, false },
    { HdmiOutput::VIC::VIC19_1280_720_P_50_16_9,   "720p50",    VideoResolution::DS_VIDEO_PIXELRES_1280X720,  VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, false },
    { HdmiOutput::VIC::VIC5_1920_1080_I_60_16_9,   "1080i",     VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, true  },
    { HdmiOutput::VIC::VIC20_1920_1080_I_50_16_9,  "1080i50",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, true  },
    { HdmiOutput::VIC::VIC16_1920_1080_P_60_16_9,  "1080p",     VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, false },
    { HdmiOutput::VIC::VIC32_1920_1080_P_24_16_9,  "1080p24",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_24, false },
    { HdmiOutput::VIC::VIC33_1920_1080_P_25_16_9,  "1080p25",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_25, false },
    { HdmiOutput::VIC::VIC34_1920_1080_P_30_16_9,  "1080p30",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_30, false },
    { HdmiOutput::VIC::VIC31_1920_1080_P_50_16_9,  "1080p50",   VideoResolution::DS_VIDEO_PIXELRES_1920X1080, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, false },
    { HdmiOutput::VIC::VIC93_3840_2160_P_24_16_9,  "2160p24",   VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_24, false },
    { HdmiOutput::VIC::VIC94_3840_2160_P_25_16_9,  "2160p25",   VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_25, false },
    { HdmiOutput::VIC::VIC95_3840_2160_P_30_16_9,  "2160p30",   VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_30, false },
    { HdmiOutput::VIC::VIC96_3840_2160_P_50_16_9,  "2160p50",   VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_50, false },
    { HdmiOutput::VIC::VIC97_3840_2160_P_60_16_9,  "2160p",     VideoResolution::DS_VIDEO_PIXELRES_3840X2160, VideoAspectRatio::DS_VIDEO_ASPECT_RATIO_16X9, VideoFrameRate::DS_VIDEO_FRAMERATE_60, false },
};

int32_t ExtractIntProperty(const std::optional<PropertyValue>& value, int32_t defaultValue)
{
    if (!value.has_value() || !value->value.has_value() || value->value->getTag() != PropertyValue::Value::intValue) {
        return defaultValue;
    }
    return value->value->get<PropertyValue::Value::intValue>();
}

const VicResolutionEntry* FindVicEntryByName(const std::string& name)
{
    for (const auto& entry : kVicTable) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

// Default video mode of a port. PlatformCapabilities.nativeFrameRate gives the territory rate
// (50 Hz or 60 Hz family); the mode itself is the profile default (TV 2160p, STB 1080p) at that
// rate, limited to what the port supports. The default is only reported here: the middleware
// applies it through Property.VIC. Returns an empty string when the port supports none of the
// candidates. `caps` may be null when the port capabilities are unknown.
std::string ResolveDefaultResolution(const HdmiOutput::Capabilities* caps)
{
    bool is50HzTerritory = false;
    HdmiOutput::PlatformCapabilities platformCaps;
    if (HdmiOutputHelper::Instance().GetPlatformCapabilities(platformCaps)) {
        const double rate = platformCaps.nativeFrameRate;
        is50HzTerritory = (std::fabs(rate - 25.0) < 0.5) || (std::fabs(rate - 50.0) < 0.5);
    } else {
        DSLOG_WARN("PlatformCapabilities unavailable, assuming a 60 Hz territory for the default resolution");
    }

    std::vector<std::string> candidates;
    const auto addFamily = [&](const char* name50, const char* name60) {
        candidates.push_back(is50HzTerritory ? name50 : name60);
        candidates.push_back(is50HzTerritory ? name60 : name50);
    };
    if (profileType == TV) {
        addFamily("2160p50", "2160p");
    }
    addFamily("1080p50", "1080p");
    addFamily("720p50", "720p");
    addFamily("576p50", "480p");

    for (const auto& name : candidates) {
        const VicResolutionEntry* entry = FindVicEntryByName(name);
        if (entry == nullptr) {
            continue;
        }
        if (caps == nullptr || std::find(caps->supportedVICs.begin(), caps->supportedVICs.end(), entry->vic) != caps->supportedVICs.end()) {
            return name;
        }
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// Sink EDID (delivered by IHDMIOutputControllerListener.onEDID()). Only the CTA-861 data blocks
// needed by the VideoPort APIs are decoded: supported VICs, HDR formats and Dolby Digital /
// Dolby Digital Plus audio.
// ---------------------------------------------------------------------------
struct SinkEdidInfo {
    std::set<uint8_t> vics;     // CTA-861 VICs, without the "native" flag
    int32_t hdrMask = 0;        // DS_HDRSTANDARD_* bits
    uint8_t maxAc3Channels = 0;
    uint8_t maxEac3Channels = 0;
};

constexpr uint32_t TvRes(const TVResolution resolution)
{
    return static_cast<uint32_t>(resolution);
}

// CTA-861 VIC -> DS_TV_RESOLUTION_* bits. Some legacy resolutions are the same timing under two
// names (for example 1080p and 1080p60), so a VIC can map to more than one bit.
struct EdidVicTvResolution {
    uint8_t vic;
    uint32_t mask;
};

const EdidVicTvResolution kEdidVicTable[] = {
    { 2,  TvRes(TVResolution::DS_TV_RESOLUTION_480P) },
    { 3,  TvRes(TVResolution::DS_TV_RESOLUTION_480P) },
    { 6,  TvRes(TVResolution::DS_TV_RESOLUTION_480I) },
    { 7,  TvRes(TVResolution::DS_TV_RESOLUTION_480I) },
    { 17, TvRes(TVResolution::DS_TV_RESOLUTION_576P) | TvRes(TVResolution::DS_TV_RESOLUTION_576P50) },
    { 18, TvRes(TVResolution::DS_TV_RESOLUTION_576P) | TvRes(TVResolution::DS_TV_RESOLUTION_576P50) },
    { 21, TvRes(TVResolution::DS_TV_RESOLUTION_576I) },
    { 22, TvRes(TVResolution::DS_TV_RESOLUTION_576I) },
    { 4,  TvRes(TVResolution::DS_TV_RESOLUTION_720P) },
    { 69, TvRes(TVResolution::DS_TV_RESOLUTION_720P) },
    { 19, TvRes(TVResolution::DS_TV_RESOLUTION_720P50) },
    { 68, TvRes(TVResolution::DS_TV_RESOLUTION_720P50) },
    { 5,  TvRes(TVResolution::DS_TV_RESOLUTION_1080I) },
    { 20, TvRes(TVResolution::DS_TV_RESOLUTION_1080I50) | TvRes(TVResolution::DS_TV_RESOLUTION_1080I25) },
    { 16, TvRes(TVResolution::DS_TV_RESOLUTION_1080P) | TvRes(TVResolution::DS_TV_RESOLUTION_1080P60) },
    { 32, TvRes(TVResolution::DS_TV_RESOLUTION_1080P24) },
    { 72, TvRes(TVResolution::DS_TV_RESOLUTION_1080P24) },
    { 34, TvRes(TVResolution::DS_TV_RESOLUTION_1080P30) },
    { 74, TvRes(TVResolution::DS_TV_RESOLUTION_1080P30) },
    { 31, TvRes(TVResolution::DS_TV_RESOLUTION_1080P50) },
    { 75, TvRes(TVResolution::DS_TV_RESOLUTION_1080P50) },
    { 93, TvRes(TVResolution::DS_TV_RESOLUTION_2160P24) },
    { 94, TvRes(TVResolution::DS_TV_RESOLUTION_2160P25) },
    { 95, TvRes(TVResolution::DS_TV_RESOLUTION_2160P30) },
    { 96, TvRes(TVResolution::DS_TV_RESOLUTION_2160P50) },
    { 97, TvRes(TVResolution::DS_TV_RESOLUTION_2160P60) },
};

// VICs 1-64 are sent with bit 7 set when the mode is the sink's native one.
uint8_t NormalizeSvd(const uint8_t svd)
{
    return (svd >= 129 && svd <= 192) ? static_cast<uint8_t>(svd & 0x7F) : svd;
}

void ParseHdmiVsdb(const uint8_t* data, const size_t len, SinkEdidInfo& info)
{
    // HDMI 1.4 LLC OUI 00-0C-03, stored little endian.
    if (len < 8 || data[0] != 0x03 || data[1] != 0x0C || data[2] != 0x00) {
        return;
    }
    const uint8_t flags = data[7];
    size_t pos = 8;
    if (flags & 0x80) {          // latency fields present
        pos += (flags & 0x40) ? 4 : 2;
    }
    if ((flags & 0x20) == 0 || pos + 1 >= len) {  // no HDMI_Video fields
        return;
    }
    const size_t hdmiVicCount = data[pos + 1] >> 5;
    pos += 2;
    // HDMI_VIC 1-3 are 3840x2160 at 30, 25 and 24 Hz, i.e. CTA VICs 95, 94 and 93.
    static const uint8_t kHdmiVicToCta[] = { 0, 95, 94, 93 };
    for (size_t i = 0; i < hdmiVicCount && pos + i < len; ++i) {
        const uint8_t hdmiVic = data[pos + i];
        if (hdmiVic >= 1 && hdmiVic <= 3) {
            info.vics.insert(kHdmiVicToCta[hdmiVic]);
        }
    }
}

void ParseExtendedDataBlock(const uint8_t* data, const size_t len, SinkEdidInfo& info)
{
    if (len < 1) {
        return;
    }
    switch (data[0]) {
    case 1:   // Vendor-specific video data block: OUI identifies Dolby Vision / HDR10+
        if (len >= 4) {
            if (data[1] == 0x46 && data[2] == 0xD0 && data[3] == 0x00) {
                info.hdrMask |= static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_DOLBYVISION);
            } else if (data[1] == 0x8B && data[2] == 0x84 && data[3] == 0x90) {
                info.hdrMask |= static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HDR10PLUS);
            }
        }
        break;
    case 6:   // HDR static metadata: bit 2 = SMPTE ST 2084 (HDR10), bit 3 = HLG
        if (len >= 2) {
            if (data[1] & 0x04) {
                info.hdrMask |= static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HDR10);
            }
            if (data[1] & 0x08) {
                info.hdrMask |= static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HLG);
            }
        }
        break;
    case 14:  // YCbCr 4:2:0 only video data block
        for (size_t i = 1; i < len; ++i) {
            if (NormalizeSvd(data[i]) != 0) {
                info.vics.insert(NormalizeSvd(data[i]));
            }
        }
        break;
    default:
        break;
    }
}

void ParseDataBlock(const uint8_t tag, const uint8_t* data, const size_t len, SinkEdidInfo& info)
{
    switch (tag) {
    case 1:   // Audio data block: 3-byte short audio descriptors
        for (size_t i = 0; i + 2 < len; i += 3) {
            const uint8_t format = (data[i] >> 3) & 0x0F;
            const uint8_t channels = static_cast<uint8_t>((data[i] & 0x07) + 1);
            if (format == 2) {          // AC-3
                info.maxAc3Channels = std::max(info.maxAc3Channels, channels);
            } else if (format == 10) {  // E-AC-3
                info.maxEac3Channels = std::max(info.maxEac3Channels, channels);
            }
        }
        break;
    case 2:   // Video data block: short video descriptors
        for (size_t i = 0; i < len; ++i) {
            if (NormalizeSvd(data[i]) != 0) {
                info.vics.insert(NormalizeSvd(data[i]));
            }
        }
        break;
    case 3:
        ParseHdmiVsdb(data, len, info);
        break;
    case 7:
        ParseExtendedDataBlock(data, len, info);
        break;
    default:
        break;
    }
}

// Every offset is bounds checked: the EDID comes from the sink and may be truncated or corrupt.
// Returns false only when the data is not an EDID at all.
bool ParseSinkEdid(const std::vector<uint8_t>& edid, SinkEdidInfo& info)
{
    static const uint8_t kHeader[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    if (edid.size() < 128 || std::memcmp(edid.data(), kHeader, sizeof(kHeader)) != 0) {
        return false;
    }
    const size_t blocks = edid.size() / 128;
    for (size_t b = 1; b < blocks; ++b) {
        const uint8_t* block = &edid[b * 128];
        if (block[0] != 0x02) {   // CTA-861 extension
            continue;
        }
        const size_t end = block[2];   // offset of the first detailed timing descriptor
        if (end <= 4 || end > 127) {
            continue;
        }
        size_t pos = 4;
        while (pos < end) {
            const uint8_t tag = block[pos] >> 5;
            const size_t len = block[pos] & 0x1F;
            if (pos + 1 + len > end) {
                break;
            }
            ParseDataBlock(tag, block + pos + 1, len, info);
            pos += 1 + len;
        }
    }
    return true;
}

// Fetches and decodes the EDID of the connected sink. `connected` is false when no sink is attached.
uint32_t ReadSinkEdid(const int32_t outputId, SinkEdidInfo& info, bool& connected)
{
    std::vector<uint8_t> edid;
    connected = true;
    switch (HdmiOutputHelper::Instance().GetEdid(outputId, edid)) {
    case HdmiOutputHelper::EdidStatus::AVAILABLE:
        break;
    case HdmiOutputHelper::EdidStatus::DISCONNECTED:
        connected = false;
        return WPEFramework::Core::ERROR_NONE;
    default:
        DSLOG_WARN("EDID not available for HDMI output id=%d", outputId);
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    if (!ParseSinkEdid(edid, info)) {
        DSLOG_ERR("EDID of HDMI output id=%d is invalid (%zu bytes)", outputId, edid.size());
        return WPEFramework::Core::ERROR_GENERAL;
    }
    return WPEFramework::Core::ERROR_NONE;
}

int32_t SurroundModeFromEdid(const SinkEdidInfo& info)
{
    // Surround means at least 5.1 channels of Dolby Digital / Dolby Digital Plus.
    if (info.maxEac3Channels >= 6) {
        return static_cast<int32_t>(VideoPortSurroundMode::DS_VIDEO_PORT_SURROUNDMODE_DDPLUS);
    }
    if (info.maxAc3Channels >= 6) {
        return static_cast<int32_t>(VideoPortSurroundMode::DS_VIDEO_PORT_SURROUNDMODE_DD);
    }
    return static_cast<int32_t>(VideoPortSurroundMode::DS_VIDEO_PORT_SURROUNDMODE_NONE);
}

uint32_t TvResolutionsFromEdid(const SinkEdidInfo& info)
{
    uint32_t mask = 0;
    for (const auto& entry : kEdidVicTable) {
        if (info.vics.count(entry.vic) != 0) {
            mask |= entry.mask;
        }
    }
    return mask;
}

ResolutionChange ToResolutionChange(const VideoResolution pixelResolution)
{
    ResolutionChange change;
    switch (pixelResolution) {
    case VideoResolution::DS_VIDEO_PIXELRES_720X480:   change.width = 720;  change.height = 480;  break;
    case VideoResolution::DS_VIDEO_PIXELRES_720X576:   change.width = 720;  change.height = 576;  break;
    case VideoResolution::DS_VIDEO_PIXELRES_1280X720:  change.width = 1280; change.height = 720;  break;
    case VideoResolution::DS_VIDEO_PIXELRES_1920X1080: change.width = 1920; change.height = 1080; break;
    case VideoResolution::DS_VIDEO_PIXELRES_3840X2160: change.width = 3840; change.height = 2160; break;
    default:                                           change.width = 1920; change.height = 1080; break;
    }
    return change;
}

dsHdcpStatus_t ToDsHdcpStatus(const VideoPortHdcpStatus status)
{
    switch (status) {
    case VideoPortHdcpStatus::DS_HDCP_STATUS_UNAUTHENTICATED:       return dsHDCP_STATUS_UNAUTHENTICATED;
    case VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATED:         return dsHDCP_STATUS_AUTHENTICATED;
    case VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATIONFAILURE: return dsHDCP_STATUS_AUTHENTICATIONFAILURE;
    case VideoPortHdcpStatus::DS_HDCP_STATUS_INPROGRESS:            return dsHDCP_STATUS_INPROGRESS;
    case VideoPortHdcpStatus::DS_HDCP_STATUS_PORTDISABLED:          return dsHDCP_STATUS_PORTDISABLED;
    default:                                                        return dsHDCP_STATUS_UNPOWERED;
    }
}
}

dVideoPortAIDLImpl::dVideoPortAIDLImpl()
    : _opened(false)
{
    InitialiseHAL();
}

dVideoPortAIDLImpl::~dVideoPortAIDLImpl()
{
    DeInitialiseHAL();
}

// The HDMI output is opened, started and closed by HdmiOutputHelper, which is shared with the
// other HDMI Output components. The first component to acquire an output opens it; the others
// attach to the same session, so a second open() is never issued.
bool dVideoPortAIDLImpl::AcquireDevice(const Output::Id& id, Device& device)
{
    HdmiOutputHelper::Callbacks callbacks;
    callbacks.onHotPlug = [this](int32_t handle, bool connected) {
        OnHotPlugDetectStateChanged(handle, connected);
    };
    callbacks.onFrameRateChanged = [this](int32_t handle) {
        OnFrameRateChanged(handle);
    };
    callbacks.onHdcpStatus = [this](int32_t handle, HdmiOutput::HDCPStatus status, HdmiOutput::HDCPProtocolVersion version) {
        OnHDCPStatusChanged(handle, status, version);
    };
    callbacks.onStateChanged = [this](int32_t handle, HdmiOutput::State oldState, HdmiOutput::State newState) {
        OnStateChanged(handle, oldState, newState);
    };

    device.id = id;
    device.lease = HdmiOutputHelper::Instance().Acquire(id.value, std::move(callbacks));
    // VideoPort drives the controller, so a lease without one is treated like a failed open().
    if (!device.lease.HasController()) {
        DSLOG_ERR("HDMI output id=%d is not available to VideoPort", id.value);
        device.lease.Reset();
        return false;
    }
    device.output = device.lease.GetOutput();
    device.controller = device.lease.GetController();
    return true;
}

dVideoPortAIDLImpl::Device* dVideoPortAIDLImpl::FindDevice(const int32_t handle)
{
    if (!_opened) {
        return nullptr;
    }
    for (auto& device : _devices) {
        if (device.id.value == handle) {
            return &device;
        }
    }
    return nullptr;
}

void dVideoPortAIDLImpl::InitialiseHAL()
{
    std::lock_guard<std::mutex> lock(_adminLock);
    if (_opened) {
        return;
    }

    std::vector<int32_t> ids;
    if (!HdmiOutputHelper::Instance().GetOutputIds(ids) || ids.empty()) {
        DSLOG_ERR("getHDMIOutputIds failed or returned no HDMI outputs");
        return;
    }

    _devices.reserve(ids.size());
    for (const int32_t value : ids) {
        Output::Id id;
        id.value = value;
        Device device;
        if (AcquireDevice(id, device)) {
            _devices.push_back(std::move(device));
            DSLOG_INFO("HDMI Output AIDL HAL initialized for VideoPort (outputId=%d)", id.value);
        }
    }

    if (_devices.empty()) {
        return;
    }
    DSLOG_INFO("%zu of %zu HDMI outputs opened", _devices.size(), ids.size());
    getPersistenceValue();

    // Legacy parity: seed the shared HDMI status cache before any event arrives.
    _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPSTATUS, dsHDCP_STATUS_UNAUTHENTICATED);
    _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPVERSION, dsHDCP_VERSION_1X);
    bool connected = false;
    if (!_devices.front().controller->getHotPlugDetectState(&connected).isOk()) {
        DSLOG_ERR("getHotPlugDetectState failed during init");
    }
    _dsSyncHdmiStatus(DS_HDMI_TAG_HOTPLUP, connected ? dsDISPLAY_EVENT_CONNECTED : dsDISPLAY_EVENT_DISCONNECTED);

    _opened = true;
}

void dVideoPortAIDLImpl::DeInitialiseHAL()
{
    std::vector<Device> devices;
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        _opened = false;
        devices.swap(_devices);
    }
    // The leases are released without _adminLock held: Reset() waits for callbacks that are already
    // running, and some of them (GetVideoPortResolution) take _adminLock. The session is closed
    // when the last component using the output releases its lease.
    for (auto& device : devices) {
        device.lease.Reset();
    }
}

void dVideoPortAIDLImpl::setAllCallbacks(const CallbackBundle& bundle)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    _callbacks = bundle;
}

void dVideoPortAIDLImpl::getPersistenceValue()
{
    // Default: profile resolution (TV 2160p, otherwise 1080p) at the territory frame rate, limited
    // to what the first output supports. Falls back to the plain profile default.
    HdmiOutput::Capabilities caps;
    const bool haveCaps = !_devices.empty() && HdmiOutputHelper::Instance().GetOutputCapabilities(_devices.front().id.value, caps);
    _persistedResolution = ResolveDefaultResolution(haveCaps ? &caps : nullptr);
    if (_persistedResolution.empty()) {
        _persistedResolution = (profileType == TV) ? "2160p" : "1080p";
    }
    _force4KDisabled = false;
    try {
        auto& persistence = device::HostPersistence::getInstance();
        _persistedResolution = persistence.getProperty("HDMI0.resolution", _persistedResolution);
        DSLOG_INFO("Persistent HDMI resolution read: %s", _persistedResolution.c_str());
        _force4KDisabled = (persistence.getProperty("VideoDevice.force4KDisabled", "false") == "true");
    } catch (...) {
        DSLOG_ERR("Error reading persistence values for VideoPort");
    }
}

uint32_t dVideoPortAIDLImpl::GetVideoPort(const VideoPortType videoPort, const int32_t index, int32_t& handle)
{
    if (!_opened || index < 0 || static_cast<size_t>(index) >= _devices.size()) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    DSLOG_INFO(" videoPort=%d, index=%d", static_cast<int>(videoPort), index);

    // Only HDMI/INTERNAL outputs are backed by the AIDL HAL.
    if (videoPort != VideoPortType::DS_VIDEO_PORT_TYPE_HDMI && videoPort != VideoPortType::DS_VIDEO_PORT_TYPE_INTERNAL) {
        DSLOG_ERR(" dsGetVideoPort failed with error: unsupported video port type");
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    handle = _devices[index].id.value;
    DSLOG_INFO(" SUCCESS - handle=%d", handle);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::IsVideoPortEnabled(const int32_t handle, bool& enabled)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::State state;
    if (!device->output->getState(&state).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    enabled = (state == HdmiOutput::State::STARTED);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::EnableVideoPort(const int32_t handle, const bool enabled)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    DSLOG_INFO(" handle=%d, enabled=%s", handle, enabled ? "true" : "false");

    HdmiOutput::State state;
    if (!device->output->getState(&state).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    // Mapping: dsEnableVideoPort -> IHDMIOutputController.start()/stop(), valid only from READY/STARTED.
    if (enabled) {
        if (state == HdmiOutput::State::STARTED) {
            return WPEFramework::Core::ERROR_NONE;
        }
        if (state != HdmiOutput::State::READY || !device->controller->start().isOk()) {
            DSLOG_ERR("start() failed, state=%d", static_cast<int>(state));
            return WPEFramework::Core::ERROR_GENERAL;
        }
    } else {
        if (state == HdmiOutput::State::READY) {
            return WPEFramework::Core::ERROR_NONE;
        }
        if (state != HdmiOutput::State::STARTED || !device->controller->stop().isOk()) {
            DSLOG_ERR("stop() failed, state=%d", static_cast<int>(state));
            return WPEFramework::Core::ERROR_GENERAL;
        }
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::IsVideoPortDisplayConnected(const int32_t handle, bool& connected)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    if (!device->controller->getHotPlugDetectState(&connected).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::IsVideoPortActive(const int32_t handle, bool& active)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // Mapping: dsIsVideoPortActive -> IHDMIOutput.getState() + IHDMIOutputController.getHotPlugDetectState().
    // onHotPlugDetectStateChanged() always fires while the output is opening, so an open output
    // with the HPD line asserted is treated as an active port.
    HdmiOutput::State state = HdmiOutput::State::UNKNOWN;
    if (!device->output->getState(&state).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    bool connected = false;
    if (!device->controller->getHotPlugDetectState(&connected).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    active = connected && (state == HdmiOutput::State::READY || state == HdmiOutput::State::STARTED);
    DSLOG_INFO("handle=%d state=%d connected=%s active=%s", handle, static_cast<int>(state),
        connected ? "true" : "false", active ? "true" : "false");
    return WPEFramework::Core::ERROR_NONE;
}

bool dVideoPortAIDLImpl::VicToResolution(HdmiOutput::VIC vic, VideoPortResolution& resolution)
{
    for (const auto& entry : kVicTable) {
        if (entry.vic == vic) {
            resolution.name = entry.name;
            resolution.pixelResolution = entry.pixelResolution;
            resolution.aspectRatio = entry.aspectRatio;
            resolution.stereoScopicMode = VideoStereoScopicMode::DS_VIDEO_SSMODE_2D;
            resolution.frameRate = entry.frameRate;
            resolution.interlaced = entry.interlaced;
            return true;
        }
    }
    return false;
}

bool dVideoPortAIDLImpl::ResolutionNameToVic(const std::string& name, HdmiOutput::VIC& vic)
{
    for (const auto& entry : kVicTable) {
        if (name == entry.name) {
            vic = entry.vic;
            return true;
        }
    }
    return false;
}

uint32_t dVideoPortAIDLImpl::GetVideoPortResolution(const int32_t handle, VideoPortResolution& resolution)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // Legacy parity: report the persisted resolution while no display is connected.
    bool connected = true;
    if (device->controller->getHotPlugDetectState(&connected).isOk() && !connected) {
        std::string persisted;
        {
            std::lock_guard<std::mutex> lock(_adminLock);
            persisted = _persistedResolution;
        }
        HdmiOutput::VIC persistedVic;
        if (ResolutionNameToVic(persisted, persistedVic) && VicToResolution(persistedVic, resolution)) {
            return WPEFramework::Core::ERROR_NONE;
        }
    }
    std::optional<PropertyValue> value;
    if (!device->output->getProperty(HdmiOutput::Property::VIC, &value).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    const HdmiOutput::VIC vic = static_cast<HdmiOutput::VIC>(ExtractIntProperty(value, 0));
    if (!VicToResolution(vic, resolution)) {
        DSLOG_WARN("VIC=%d has no mapped resolution name, defaulting to 1080p", static_cast<int>(vic));
        VicToResolution(HdmiOutput::VIC::VIC16_1920_1080_P_60_16_9, resolution);
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetVideoPortResolution(const int32_t handle, const VideoPortResolution resolution, const bool persist, const bool forceCompatibility)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::VIC vic;
    if (!ResolutionNameToVic(resolution.name, vic)) {
        DSLOG_ERR("Unsupported resolution name '%s' for AIDL HDMI Output HAL", resolution.name.c_str());
        return WPEFramework::Core::ERROR_BAD_REQUEST;
    }

    bool connected = false;
    if (device->controller->getHotPlugDetectState(&connected).isOk() && !connected) {
        DSLOG_INFO("Port not connected, ignoring resolution request");
        return WPEFramework::Core::ERROR_GENERAL;
    }

    if (_force4KDisabled && resolution.name.find("2160") != std::string::npos) {
        DSLOG_INFO("Cannot set 4K resolution while force-disable-4K is enabled");
        return WPEFramework::Core::ERROR_GENERAL;
    }

    // Skip the HAL (and the AVMUTE it causes) when the requested VIC is already active.
    bool alreadyActive = false;
    std::optional<PropertyValue> currentValue;
    if (device->output->getProperty(HdmiOutput::Property::VIC, &currentValue).isOk()) {
        alreadyActive = (ExtractIntProperty(currentValue, -1) == static_cast<int32_t>(vic));
    }

    std::function<void(const ResolutionChange)> preChange;
    std::function<void(const ResolutionChange)> postChange;
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        preChange = _callbacks.OnResolutionPreChange;
        postChange = _callbacks.OnResolutionPostChange;
    }
    const ResolutionChange change = ToResolutionChange(resolution.pixelResolution);

    if (!alreadyActive) {
        if (preChange) {
            preChange(change);
        }
        PropertyValue value;
        value.value = PropertyValue::Value::make<PropertyValue::Value::intValue>(static_cast<int32_t>(vic));
        bool applied = false;
        if (!device->controller->setProperty(HdmiOutput::Property::VIC, value, &applied).isOk() || !applied) {
            DSLOG_ERR("setProperty(VIC) failed for '%s'", resolution.name.c_str());
            return WPEFramework::Core::ERROR_GENERAL;
        }
        if (postChange) {
            postChange(change);
        }
    } else {
        DSLOG_INFO("Same resolution requested, skipping setProperty(VIC)");
    }

    {
        std::lock_guard<std::mutex> lock(_adminLock);
        _persistedResolution = resolution.name;
    }
    if (persist) {
        try {
            device::HostPersistence::getInstance().persistHostProperty("HDMI0.resolution", resolution.name);
        } catch (...) {
            DSLOG_ERR("Failed to persist HDMI resolution");
        }
    }
    // No analog ports behind the AIDL HAL, so there is no cross-port compatibility to enforce.
    (void)forceCompatibility;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::getIgnoreEDIDStatus(const int32_t handle, bool& ignoreEDID)
{
    if (FindDevice(handle) == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    ignoreEDID = false;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetColorDepth(const int32_t handle, uint32_t& colorDepth)
{
    (void)handle;
    colorDepth = 0;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetVideoPortColorDepth(const int32_t handle, const uint32_t colorDepth)
{
    (void)handle;
    (void)colorDepth;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::GetQuantizationRange(const int32_t handle, VideoPortQuantizationRange& quantizationRange)
{
    (void)handle;
    quantizationRange = VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_UNKNOWN;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetVideoPortQuantizationRange(const int32_t handle, const VideoPortQuantizationRange quantizationRange)
{
    (void)handle;
    (void)quantizationRange;
    DSLOG_WARN("quantization range is a read-only sink attribute; not supported by AIDL HAL");
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetColorSpace(const int32_t handle, VideoPortColorSpace& colorSpace)
{
    (void)handle;
    colorSpace = VideoPortColorSpace::DS_DISPLAY_COLORSPACE_UNKNOWN;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetColorSpace(const int32_t handle, const VideoPortColorSpace colorSpace)
{
    (void)handle;
    (void)colorSpace;
    DSLOG_WARN("color space is a read-only, EDID-negotiated attribute; not supported by AIDL HAL");
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetVideoPortFrameRate(const int32_t handle, uint32_t& frameRate)
{
    VideoPortResolution resolution;
    const uint32_t result = GetVideoPortResolution(handle, resolution);
    if (result != WPEFramework::Core::ERROR_NONE) {
        return result;
    }
    switch (resolution.frameRate) {
    case VideoFrameRate::DS_VIDEO_FRAMERATE_24: frameRate = 24; break;
    case VideoFrameRate::DS_VIDEO_FRAMERATE_25: frameRate = 25; break;
    case VideoFrameRate::DS_VIDEO_FRAMERATE_30: frameRate = 30; break;
    case VideoFrameRate::DS_VIDEO_FRAMERATE_50: frameRate = 50; break;
    case VideoFrameRate::DS_VIDEO_FRAMERATE_60: frameRate = 60; break;
    default: frameRate = 60; break;
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetVideoPortFrameRate(const int32_t handle, const uint32_t frameRate)
{
    DSLOG_WARN(" not a separate HAL operation — frame rate is implicit in SetVideoPortResolution");
    return WPEFramework::Core::ERROR_NONE;
}

VideoPortHdcpStatus dVideoPortAIDLImpl::ConvertHdcpStatus(HdmiOutput::HDCPStatus status)
{
    switch (status) {
    case HdmiOutput::HDCPStatus::UNAUTHENTICATED: return VideoPortHdcpStatus::DS_HDCP_STATUS_UNAUTHENTICATED;
    case HdmiOutput::HDCPStatus::AUTHENTICATION_IN_PROGRESS: return VideoPortHdcpStatus::DS_HDCP_STATUS_INPROGRESS;
    case HdmiOutput::HDCPStatus::AUTHENTICATION_FAILURE: return VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATIONFAILURE;
    case HdmiOutput::HDCPStatus::AUTHENTICATED: return VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATED;
    default: return VideoPortHdcpStatus::DS_HDCP_STATUS_UNPOWERED;
    }
}

VideoPortHdcpProtocolVersion dVideoPortAIDLImpl::ConvertHdcpVersion(HdmiOutput::HDCPProtocolVersion version)
{
    return version == HdmiOutput::HDCPProtocolVersion::VERSION_2_X
        ? VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_2X
        : VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X;
}

HdmiOutput::HDCPProtocolVersion dVideoPortAIDLImpl::ConvertHdcpVersion(const VideoPortHdcpProtocolVersion version)
{
    return version == VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_2X
        ? HdmiOutput::HDCPProtocolVersion::VERSION_2_X
        : HdmiOutput::HDCPProtocolVersion::VERSION_1_X;
}

uint32_t dVideoPortAIDLImpl::GetVideoPortHDCPStatus(const int32_t handle, VideoPortHdcpStatus& hdcpStatus)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::HDCPStatus status;
    if (!device->controller->getHDCPStatus(&status).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    hdcpStatus = ConvertHdcpStatus(status);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetHDCPProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::Capabilities capabilities;
    if (!device->output->getCapabilities(&capabilities).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    bool supports2x = false;
    for (const auto& supported : capabilities.supportedHDCPProtocolVersions) {
        supports2x = supports2x || (supported == HdmiOutput::HDCPProtocolVersion::VERSION_2_X);
    }
    hdcpVersion = supports2x ? VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_2X : VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetHDCPReceiverProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::HDCPProtocolVersion version;
    if (!device->controller->getHDCPReceiverVersion(&version).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    hdcpVersion = ConvertHdcpVersion(version);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetHDCPCurrentProtocolVersionOnVideoPort(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::HDCPProtocolVersion version;
    if (!device->controller->getHDCPCurrentVersion(&version).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    hdcpVersion = ConvertHdcpVersion(version);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::EnableHDCPOnVideoPort(const int32_t handle, const bool hdcpEnable, const uint8_t* hdcpKey, const uint16_t hdcpKeySize)
{
    (void)handle;
    (void)hdcpEnable;
    (void)hdcpKey;
    (void)hdcpKeySize;
    DSLOG_WARN("HDCP enable/disable is not exposed by the AIDL HDMI Output controller");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::IsHDCPEnabledOnVideoPort(const int32_t handle, bool& hdcpEnabled)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::HDCPStatus status;
    if (!device->controller->getHDCPStatus(&status).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    hdcpEnabled = (status != HdmiOutput::HDCPStatus::UNKNOWN);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetTVHDRCapabilities(const int32_t handle, int32_t& capabilities)
{
    if (FindDevice(handle) == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // Mapping: dsGetTVHDRCapabilities -> IHDMIOutputControllerListener.onEDID(), i.e. the sink's EDID.
    SinkEdidInfo info;
    bool connected = false;
    const uint32_t result = ReadSinkEdid(handle, info, connected);
    if (result != WPEFramework::Core::ERROR_NONE) {
        return result;
    }
    capabilities = info.hdrMask;
    DSLOG_INFO("handle=%d connected=%s TV HDR capabilities=0x%x", handle, connected ? "true" : "false", capabilities);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetTVSupportedResolutions(const int32_t handle, int32_t& resolutions)
{
    if (FindDevice(handle) == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // Mapping: dsSupportedTvResolutions -> IHDMIOutputControllerListener.onEDID(), i.e. the sink's EDID.
    SinkEdidInfo info;
    bool connected = false;
    const uint32_t result = ReadSinkEdid(handle, info, connected);
    if (result != WPEFramework::Core::ERROR_NONE) {
        return result;
    }
    resolutions = static_cast<int32_t>(TvResolutionsFromEdid(info));
    DSLOG_INFO("handle=%d connected=%s TV resolutions=0x%x (%zu VICs)", handle, connected ? "true" : "false",
        resolutions, info.vics.size());
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetForceDisable4K(const int32_t handle, const bool disable)
{
    if (FindDevice(handle) == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // The AIDL HAL has no 4K switch; the flag is enforced in SetVideoPortResolution and persisted.
    _force4KDisabled = disable;
    try {
        device::HostPersistence::getInstance().persistHostProperty("VideoDevice.force4KDisabled", disable ? "true" : "false");
    } catch (...) {
        DSLOG_ERR("Failed to persist force4KDisabled");
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetForceDisable4K(const int32_t handle, bool& disabled)
{
    disabled = false;
    if (FindDevice(handle) == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    disabled = _force4KDisabled;
    return WPEFramework::Core::ERROR_NONE;
}

HDRStandard dVideoPortAIDLImpl::ConvertHdrOutputMode(HdmiOutput::HDROutputMode mode)
{
    switch (mode) {
    case HdmiOutput::HDROutputMode::HLG:          return HDRStandard::DS_HDRSTANDARD_HLG;
    case HdmiOutput::HDROutputMode::HDR10:        return HDRStandard::DS_HDRSTANDARD_HDR10;
    case HdmiOutput::HDROutputMode::HDR10_PLUS:   return HDRStandard::DS_HDRSTANDARD_HDR10PLUS;
    case HdmiOutput::HDROutputMode::DOLBY_VISION: return HDRStandard::DS_HDRSTANDARD_DOLBYVISION;
    default:                                      return HDRStandard::DS_HDRSTANDARD_SDR;
    }
}

HdmiOutput::HDROutputMode dVideoPortAIDLImpl::ConvertHdrStandard(const HDRStandard hdrStandard)
{
    switch (hdrStandard) {
    case HDRStandard::DS_HDRSTANDARD_HLG:         return HdmiOutput::HDROutputMode::HLG;
    case HDRStandard::DS_HDRSTANDARD_HDR10:       return HdmiOutput::HDROutputMode::HDR10;
    case HDRStandard::DS_HDRSTANDARD_HDR10PLUS:   return HdmiOutput::HDROutputMode::HDR10_PLUS;
    case HDRStandard::DS_HDRSTANDARD_DOLBYVISION: return HdmiOutput::HDROutputMode::DOLBY_VISION;
    default:                                      return HdmiOutput::HDROutputMode::AUTO;
    }
}

uint32_t dVideoPortAIDLImpl::IsVideoPortOutputHDR(const int32_t handle, bool& isHDR)
{
    HDRStandard hdrStandard;
    const uint32_t result = GetVideoEOTF(handle, hdrStandard);
    if (result != WPEFramework::Core::ERROR_NONE) {
        return result;
    }
    isHDR = (hdrStandard != HDRStandard::DS_HDRSTANDARD_SDR && hdrStandard != HDRStandard::DS_HDRSTANDARD_NONE);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::ResetVideoPortOutputToSDR()
{
    if (!_opened) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    PropertyValue value;
    value.value = PropertyValue::Value::make<PropertyValue::Value::intValue>(static_cast<int32_t>(HdmiOutput::HDROutputMode::AUTO));
    uint32_t result = WPEFramework::Core::ERROR_NONE;
    for (auto& device : _devices) {
        bool applied = false;
        if (!device.controller->setProperty(HdmiOutput::Property::HDR_OUTPUT_MODE, value, &applied).isOk() || !applied) {
            DSLOG_ERR("Failed to reset HDR output mode on HDMI output id=%d", device.id.value);
            result = WPEFramework::Core::ERROR_GENERAL;
        }
    }
    return result;
}

uint32_t dVideoPortAIDLImpl::GetHDMIPreference(const int32_t handle, VideoPortHdcpProtocolVersion& hdcpVersion)
{
    (void)handle;
    hdcpVersion = VideoPortHdcpProtocolVersion::DS_HDCP_VERSION_1X;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::SetHDMIPreference(const int32_t handle, const VideoPortHdcpProtocolVersion hdcpVersion)
{
    (void)handle;
    (void)hdcpVersion;
    DSLOG_WARN("HDMI HDCP preference is not exposed by the AIDL HDMI Output HAL");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::GetVideoEOTF(const int32_t handle, HDRStandard& hdrStandard)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    std::optional<PropertyValue> value;
    if (!device->output->getProperty(HdmiOutput::Property::HDR_OUTPUT_MODE, &value).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    const auto mode = static_cast<HdmiOutput::HDROutputMode>(ExtractIntProperty(value, static_cast<int32_t>(HdmiOutput::HDROutputMode::AUTO)));
    hdrStandard = ConvertHdrOutputMode(mode);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetMatrixCoefficients(const int32_t handle, DisplayMatrixCoefficients& matrixCoefficients)
{
    (void)handle;
    matrixCoefficients = DisplayMatrixCoefficients::DS_DISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoPortAIDLImpl::IsVideoPortDisplaySurround(const int32_t handle, bool& surround)
{
    VideoPortSurroundMode mode = VideoPortSurroundMode::DS_VIDEO_PORT_SURROUNDMODE_NONE;
    const uint32_t result = GetVideoPortDisplaySurroundMode(handle, mode);
    if (result != WPEFramework::Core::ERROR_NONE) {
        surround = false;
        return result;
    }
    surround = (mode != VideoPortSurroundMode::DS_VIDEO_PORT_SURROUNDMODE_NONE);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetVideoPortDisplaySurroundMode(const int32_t handle, VideoPortSurroundMode& surroundMode)
{
    surroundMode = VideoPortSurroundMode::DS_VIDEO_PORT_SURROUNDMODE_NONE;
    if (FindDevice(handle) == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // A sink device reports its output formats through the audiomixer OutputPortProperty
    // SUPPORTED_AUDIO_FORMATS / DOLBY_ATMOS_SUPPORT, which this component does not use.
    if (profileType == TV) {
        DSLOG_WARN("surround mode of a sink device comes from audiomixer, not hdmioutput");
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // Source device: the audio formats of the connected sink come from its EDID (onEDID()).
    SinkEdidInfo info;
    bool connected = false;
    const uint32_t result = ReadSinkEdid(handle, info, connected);
    if (result != WPEFramework::Core::ERROR_NONE) {
        return result;
    }
    surroundMode = static_cast<VideoPortSurroundMode>(SurroundModeFromEdid(info));
    DSLOG_INFO("handle=%d connected=%s AC-3 channels=%u E-AC-3 channels=%u surroundMode=%d", handle,
        connected ? "true" : "false", static_cast<unsigned>(info.maxAc3Channels),
        static_cast<unsigned>(info.maxEac3Channels), static_cast<int>(surroundMode));
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetCurrentOutputSettings(const int32_t handle, DSOutputSettings& outputSettings)
{
    if (FindDevice(handle) == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HDRStandard hdrStandard = HDRStandard::DS_HDRSTANDARD_SDR;
    GetVideoEOTF(handle, hdrStandard);
    outputSettings.videoEotf = hdrStandard;
    outputSettings.matrixCoefficients = DisplayMatrixCoefficients::DS_DISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
    outputSettings.colorSpace = VideoPortColorSpace::DS_DISPLAY_COLORSPACE_UNKNOWN;
    outputSettings.quantizationRange = VideoPortQuantizationRange::DS_DISPLAY_QUANTIZATIONRANGE_UNKNOWN;

    uint32_t colorDepthCapabilities = 0;
    GetColorDepthCapabilities(handle, colorDepthCapabilities);
    outputSettings.colorDepth = colorDepthCapabilities & 0x01 ? 8 : (colorDepthCapabilities & 0x02 ? 10 : 0);
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetBackgroundColor(const int32_t handle, const VideoBackgroundColor backgroundColor)
{
    (void)handle;
    (void)backgroundColor;
    DSLOG_WARN("background color is not supported by the AIDL HDMI Output HAL");
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetForceHDRMode(const int32_t handle, const HDRStandard hdrMode)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    PropertyValue value;
    value.value = PropertyValue::Value::make<PropertyValue::Value::intValue>(static_cast<int32_t>(ConvertHdrStandard(hdrMode)));
    bool applied = false;
    if (!device->controller->setProperty(HdmiOutput::Property::HDR_OUTPUT_MODE, value, &applied).isOk() || !applied) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetColorDepthCapabilities(const int32_t handle, uint32_t& colorDepthCapabilities)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    HdmiOutput::Capabilities caps;
    if (!device->output->getCapabilities(&caps).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    uint32_t bitmask = 0x08; // dsDISPLAY_COLORDEPTH_AUTO
    for (const int32_t depth : caps.supportedColorDepths) {
        switch (depth) {
        case 8:  bitmask |= 0x01; break;
        case 10: bitmask |= 0x02; break;
        case 12: bitmask |= 0x04; break;
        default: break;
        }
    }
    colorDepthCapabilities = bitmask;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::GetPreferredColorDepth(const int32_t handle, DisplayColorDepth& colorDepth, const bool persist)
{
    (void)handle;
    colorDepth = DisplayColorDepth::DS_DISPLAY_COLORDEPTH_UNKNOWN;
    if (!persist) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    colorDepth = DisplayColorDepth::DS_DISPLAY_COLORDEPTH_AUTO;
    try {
        const std::string stored = device::HostPersistence::getInstance().getProperty("HDMI0.colorDepth",
            std::to_string(static_cast<int>(colorDepth)));
        colorDepth = static_cast<DisplayColorDepth>(std::stoi(stored));
    } catch (...) {
        DSLOG_ERR("Reading HDMI persistent color depth failed, using default");
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoPortAIDLImpl::SetPreferredColorDepth(const int32_t handle, const DisplayColorDepth colorDepth, const bool persist)
{
    (void)handle;
    // The AIDL HAL cannot apply a color depth; only the persisted preference is stored.
    if (!persist) {
        DSLOG_WARN("preferred color depth is not exposed by the AIDL HDMI Output HAL");
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    try {
        device::HostPersistence::getInstance().persistHostProperty("HDMI0.colorDepth", std::to_string(static_cast<int>(colorDepth)));
    } catch (...) {
        DSLOG_ERR("Failed to persist color depth setting");
        return WPEFramework::Core::ERROR_GENERAL;
    }
    return WPEFramework::Core::ERROR_NONE;
}

void dVideoPortAIDLImpl::OnHotPlugDetectStateChanged(const int32_t handle, bool connected)
{
    DSLOG_INFO("outputId=%d, connected=%s", handle, connected ? "true" : "false");
    // No hotplug slot exists in hal::dVideoPort::CallbackBundle; only the shared status cache is updated.
    _dsSyncHdmiStatus(DS_HDMI_TAG_HOTPLUP, connected ? dsDISPLAY_EVENT_CONNECTED : dsDISPLAY_EVENT_DISCONNECTED);
}

void dVideoPortAIDLImpl::OnFrameRateChanged(const int32_t handle)
{
    VideoPortResolution resolution;
    if (GetVideoPortResolution(handle, resolution) != WPEFramework::Core::ERROR_NONE) {
        return;
    }
    // Copied under the lock because setAllCallbacks() can replace the callbacks concurrently.
    std::function<void(const ResolutionChange)> postChange;
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        postChange = _callbacks.OnResolutionPostChange;
    }
    if (postChange) {
        postChange(ToResolutionChange(resolution.pixelResolution));
    }
}

void dVideoPortAIDLImpl::OnHDCPStatusChanged(const int32_t handle, HdmiOutput::HDCPStatus status, HdmiOutput::HDCPProtocolVersion version)
{
    DSLOG_INFO("outputId=%d, hdcpStatus=%d", handle, static_cast<int>(status));
    const VideoPortHdcpStatus converted = ConvertHdcpStatus(status);
    _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPSTATUS, ToDsHdcpStatus(converted));
    const bool authenticated = (converted == VideoPortHdcpStatus::DS_HDCP_STATUS_AUTHENTICATED);
    _dsSyncHdmiStatus(DS_HDMI_TAG_HDCPVERSION,
        (authenticated && version == HdmiOutput::HDCPProtocolVersion::VERSION_2_X) ? dsHDCP_VERSION_2X : dsHDCP_VERSION_1X);
    std::function<void(const VideoPortHdcpStatus)> hdcpCallback;
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        hdcpCallback = _callbacks.OnHDCPStatusChange;
    }
    if (hdcpCallback) {
        hdcpCallback(converted);
    }
}

void dVideoPortAIDLImpl::OnStateChanged(const int32_t handle, HdmiOutput::State oldState, HdmiOutput::State newState)
{
    DSLOG_INFO("outputId=%d, oldState=%d, newState=%d", handle, static_cast<int>(oldState), static_cast<int>(newState));
}

// ---------------------------------------------------------------------------
// Config vectors built from IHDMIOutput.getCapabilities(). The DS HAL config tables
// (kVideoPortConfigs, ...) do not exist with the AIDL HAL, so DeviceSettingsHAL::Populate*
// fall back to these when the dlsym lookup fails.
// ---------------------------------------------------------------------------
namespace {
template <typename T>
std::string JoinValues(const std::vector<T>& values)
{
    std::string out;
    for (const auto& value : values) {
        if (!out.empty()) {
            out += ",";
        }
        out += std::to_string(static_cast<int>(value));
    }
    return out;
}

const VicResolutionEntry* FindVicEntry(const HdmiOutput::VIC vic)
{
    for (const auto& entry : kVicTable) {
        if (entry.vic == vic) {
            return &entry;
        }
    }
    return nullptr;
}

void LogCapabilities(const int32_t id, const HdmiOutput::Capabilities& caps)
{
    std::string vics;
    for (const auto vic : caps.supportedVICs) {
        const VicResolutionEntry* entry = FindVicEntry(vic);
        if (!vics.empty()) {
            vics += ",";
        }
        vics += std::to_string(static_cast<int>(vic)) + "(" + (entry ? entry->name : "unmapped") + ")";
    }

    DSLOG_INFO("---- IHDMIOutput.getCapabilities() outputId=%d ----", id);
    DSLOG_INFO("supportedVersions=[%s]", JoinValues(caps.supportedVersions).c_str());
    DSLOG_INFO("supportedVICs(%zu)=[%s]", caps.supportedVICs.size(), vics.c_str());
    DSLOG_INFO("supportedHDROutputModes=[%s] supportsForcedHDROutputModes=%d",
        JoinValues(caps.supportedHDROutputModes).c_str(), caps.supportsForcedHDROutputModes);
    DSLOG_INFO("supportedHDCPProtocolVersions=[%s]", JoinValues(caps.supportedHDCPProtocolVersions).c_str());
    DSLOG_INFO("supportedColorimetries=[%s] supportedExtendedColorimetries=[%s] supportedAdditionalColorimetryExtensions=[%s]",
        JoinValues(caps.supportedColorimetries).c_str(),
        JoinValues(caps.supportedExtendedColorimetries).c_str(),
        JoinValues(caps.supportedAdditionalColorimetryExtensions).c_str());
    DSLOG_INFO("supportedColorDepths=[%s] supportedPixelFormats=[%s]",
        JoinValues(caps.supportedColorDepths).c_str(), JoinValues(caps.supportedPixelFormats).c_str());
    DSLOG_INFO("supports3D=%d supportsFRL=%d supportsVRR=%d supportsFreeSync=%d supportsQMS=%d supportsALLM=%d supportsQFT=%d",
        caps.supports3D, caps.supportsFRL, caps.supportsVRR, caps.supportsFreeSync,
        caps.supportsQMS, caps.supportsALLM, caps.supportsQFT);
}

// Reads (and logs, once per process) the capabilities of every HDMI output.
bool GetAllCapabilities(std::vector<int32_t>& ids, std::vector<HdmiOutput::Capabilities>& caps)
{
    static std::atomic<bool> logged { false };

    if (!HdmiOutputHelper::Instance().GetOutputIds(ids) || ids.empty()) {
        DSLOG_ERR("no HDMI outputs reported by getHDMIOutputIds");
        return false;
    }
    caps.assign(ids.size(), HdmiOutput::Capabilities());
    for (size_t i = 0; i < ids.size(); ++i) {
        if (!HdmiOutputHelper::Instance().GetOutputCapabilities(ids[i], caps[i])) {
            return false;
        }
    }

    if (!logged.exchange(true)) {
        HdmiOutput::PlatformCapabilities platformCaps;
        if (HdmiOutputHelper::Instance().GetPlatformCapabilities(platformCaps)) {
            DSLOG_INFO("---- IHDMIOutputManager.getCapabilities() nativeFrameRate=%f freeSync=%d ----",
                platformCaps.nativeFrameRate, static_cast<int>(platformCaps.freeSync));
        }
        for (size_t i = 0; i < ids.size(); ++i) {
            LogCapabilities(ids[i], caps[i]);
        }
    }
    return true;
}

// Resolution entries for the VICs supported by any output, without duplicates.
std::vector<const VicResolutionEntry*> CollectResolutions(const std::vector<HdmiOutput::Capabilities>& caps)
{
    std::vector<const VicResolutionEntry*> entries;
    std::set<std::string> seen;
    for (const auto& cap : caps) {
        for (const auto vic : cap.supportedVICs) {
            const VicResolutionEntry* entry = FindVicEntry(vic);
            if (entry != nullptr && seen.insert(entry->name).second) {
                entries.push_back(entry);
            }
        }
    }
    return entries;
}
}

namespace DeviceSettingsHAL {

bool PopulateVideoPortConfigFromAIDL(
    std::vector<VideoPortTypeConfig>& videoPortTypes,
    std::vector<VideoPortPortConfig>& videoPorts)
{
    std::vector<int32_t> ids;
    std::vector<HdmiOutput::Capabilities> caps;
    if (!GetAllCapabilities(ids, caps)) {
        return false;
    }

    const auto resolutions = CollectResolutions(caps);
    std::string names;
    bool hdcpSupported = false;
    for (const auto* entry : resolutions) {
        names += (names.empty() ? "" : ",");
        names += entry->name;
    }
    for (const auto& cap : caps) {
        hdcpSupported = hdcpSupported || !cap.supportedHDCPProtocolVersions.empty();
    }

    VideoPortTypeConfig typeCfg;
    typeCfg.typeId = VideoPortType::DS_VIDEO_PORT_TYPE_HDMI;
    typeCfg.name = "HDMI";
    typeCfg.dtcpSupported = false;
    typeCfg.hdcpSupported = hdcpSupported;
    typeCfg.restrictedResolution = -1;
    typeCfg.supportedResolutionNames = names;
    videoPortTypes.push_back(typeCfg);

    // Fallback when a port supports none of the territory defaults: same rule as before.
    std::string fallbackResolution = (profileType == TV) ? "2160p" : "1080p";
    if (!resolutions.empty() && (names.find(fallbackResolution) == std::string::npos)) {
        fallbackResolution = resolutions.back()->name;
    }

    for (size_t i = 0; i < ids.size(); ++i) {
        // Mapping: getDefaultResolution -> PlatformCapabilities.nativeFrameRate (territory rate),
        // limited to the VICs of this port. The middleware applies the mode through Property.VIC.
        std::string defaultResolution = ResolveDefaultResolution(&caps[i]);
        if (defaultResolution.empty()) {
            defaultResolution = fallbackResolution;
        }
        DSLOG_INFO("HDMI output id=%d default resolution=%s", ids[i], defaultResolution.c_str());

        VideoPortPortConfig portCfg;
        portCfg.videoPortType = VideoPortType::DS_VIDEO_PORT_TYPE_HDMI;
        portCfg.videoPortIndex = static_cast<int32_t>(i);
        portCfg.connectedAudioPortType = static_cast<int32_t>(AudioPortType::AUDIO_PORT_TYPE_HDMI);
        portCfg.connectedAudioPortIndex = static_cast<int32_t>(i);
        portCfg.defaultResolution = defaultResolution;
        videoPorts.push_back(portCfg);
    }

    DSLOG_INFO("Built config from HDMI Output capabilities (videoPortTypes=%zu videoPorts=%zu)",
        videoPortTypes.size(), videoPorts.size());
    return true;
}

bool PopulateVideoPortResolutionConfigFromAIDL(
    const VideoPortType videoPortType,
    std::vector<VideoPortResolution>& resolutions)
{
    if (videoPortType != VideoPortType::DS_VIDEO_PORT_TYPE_HDMI) {
        return false;
    }
    std::vector<int32_t> ids;
    std::vector<HdmiOutput::Capabilities> caps;
    if (!GetAllCapabilities(ids, caps)) {
        return false;
    }

    for (const auto* entry : CollectResolutions(caps)) {
        VideoPortResolution resCfg;
        resCfg.name = entry->name;
        resCfg.pixelResolution = entry->pixelResolution;
        resCfg.aspectRatio = entry->aspectRatio;
        resCfg.stereoScopicMode = VideoStereoScopicMode::DS_VIDEO_SSMODE_2D;
        resCfg.frameRate = entry->frameRate;
        resCfg.interlaced = entry->interlaced;
        resolutions.push_back(resCfg);
    }
    DSLOG_INFO("Built resolution config from HDMI Output capabilities (resolutions=%zu)", resolutions.size());
    return true;
}

// DFC (zoom) is an IPlaneControl ASPECT_RATIO property, not part of IHDMIOutput capabilities,
// so only the FULL/NONE modes that dVideoDeviceAIDLImpl supports are reported.
bool PopulateVideoDeviceConfigFromAIDL(
    std::vector<VideoDeviceConfigInfo>& videoDeviceConfigs)
{
    std::vector<int32_t> ids;
    std::vector<HdmiOutput::Capabilities> caps;
    if (!GetAllCapabilities(ids, caps)) {
        return false;
    }

    VideoDeviceConfigInfo videoCfg;
    videoCfg.numSupportedDFCs = 2;
    videoCfg.supportedDFCsMask =
        (1u << static_cast<uint32_t>(VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_NONE)) |
        (1u << static_cast<uint32_t>(VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL));
    videoCfg.defaultDFC = VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL;
    videoDeviceConfigs.push_back(videoCfg);
    return true;
}

} // namespace DeviceSettingsHAL
