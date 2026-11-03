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
#include "dVideoDeviceAIDLImpl.h"
#include "DeviceSettingsTypes.h"

// RPC::IteratorType, used to build the codec profile iterator
#include <com/com.h>
#include <core/core.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/hdmioutput/Capabilities.h>
#include <com/rdk/hal/hdmioutput/HDROutputMode.h>
#include <com/rdk/hal/hdmioutput/State.h>
#ifdef ENABLE_VIDEODECODER_AIDL
#include <com/rdk/hal/videodecoder/Capabilities.h>
#include <com/rdk/hal/videodecoder/CodecCapabilities.h>
#include <com/rdk/hal/videodecoder/Profile.h>
#endif

#include <algorithm>
#include <iterator>
#include <vector>

namespace HdmiOutput = com::rdk::hal::hdmioutput;

namespace {
// ---------------------------------------------------------------------------
// IVideoDecoder capabilities (model in dVideoDeviceAIDLImpl.h, namespace VideoDecoderCaps).
// The values come from IVideoDecoder.getCapabilities() (see GetDecoderCapabilities).
// ---------------------------------------------------------------------------
namespace DecoderCaps {
using namespace VideoDecoderCaps;

#ifdef ENABLE_VIDEODECODER_AIDL
// Adds the capabilities of one decoder to `out`. Several decoders are merged: dynamic ranges are
// de-duplicated and the profiles of a codec are appended to its existing entry.
bool AppendAidlCapabilities(const android::sp<com::rdk::hal::videodecoder::IVideoDecoder>& decoder, Capabilities& out)
{
    com::rdk::hal::videodecoder::Capabilities aidlCaps;
    if (decoder == nullptr || !decoder->getCapabilities(&aidlCaps).isOk()) {
        return false;
    }
    for (const auto aidlRange : aidlCaps.supportedDynamicRanges) {
        const auto range = static_cast<DynamicRange>(static_cast<int32_t>(aidlRange));
        if (std::find(out.supportedDynamicRanges.begin(), out.supportedDynamicRanges.end(), range) == out.supportedDynamicRanges.end()) {
            out.supportedDynamicRanges.push_back(range);
        }
    }
    for (const auto& aidlCodec : aidlCaps.supportedCodecs) {
        const auto codec = static_cast<Codec>(static_cast<int32_t>(aidlCodec.codec));
        auto entry = std::find_if(out.supportedCodecs.begin(), out.supportedCodecs.end(),
            [codec](const CodecCapabilities& item) { return item.codec == codec; });
        if (entry == out.supportedCodecs.end()) {
            out.supportedCodecs.push_back({ codec, {} });
            entry = std::prev(out.supportedCodecs.end());
        }
        for (const auto& aidlProfile : aidlCodec.profiles) {
            entry->profiles.push_back({
                static_cast<CodecProfile>(static_cast<int32_t>(aidlProfile.profile)),
                static_cast<CodecLevel>(static_cast<int32_t>(aidlProfile.maxLevel)),
                aidlProfile.maxBitrateInBps });
        }
    }
    return true;
}
#endif

// Legacy dsHDRSTANDARD_* bit of a decoder dynamic range; 0 when it has none (SDR, UNKNOWN).
int32_t ToHdrBit(const DynamicRange range)
{
    switch (range) {
    case DynamicRange::HLG:          return static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HLG);
    case DynamicRange::HDR10:        return static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HDR10);
    case DynamicRange::HDR10_PLUS:   return static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HDR10PLUS);
    case DynamicRange::DOLBY_VISION: return static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_DOLBYVISION);
    default:                         return 0;
    }
}

// Legacy dsVideoCodingFormat_t of a decoder codec. VP9 and AV1 have no legacy equivalent.
bool ToVideoDeviceCodec(const Codec codec, VideoDeviceCodec& legacyCodec)
{
    switch (codec) {
    case Codec::MPEG2_VIDEO: legacyCodec = VideoDeviceCodec::DS_VIDEO_CODEC_MPEG2;       return true;
    case Codec::H264_AVC:    legacyCodec = VideoDeviceCodec::DS_VIDEO_CODEC_MPEG4PART10; return true;
    case Codec::H265_HEVC:   legacyCodec = VideoDeviceCodec::DS_VIDEO_CODEC_MPEGHPART2;  return true;
    default:                 return false;
    }
}

// The legacy profile list only describes HEVC, so profiles of other codecs have no equivalent.
bool ToHevcProfile(const CodecProfile profile, VideoDeviceCodecHEVCProfile& legacyProfile)
{
    switch (profile) {
    case CodecProfile::H265_MAIN:
        legacyProfile = VideoDeviceCodecHEVCProfile::DS_VIDEO_CODEC_HEVC_PROFILE_MAIN;
        return true;
    case CodecProfile::H265_MAIN_10:
    case CodecProfile::H265_MAIN_10_HDR10:
        legacyProfile = VideoDeviceCodecHEVCProfile::DS_VIDEO_CODEC_HEVC_PROFILE_MAIN10;
        return true;
    default:
        return false;
    }
}

bool ToHevcLevel(const CodecLevel level, float& legacyLevel)
{
    switch (level) {
    case CodecLevel::H265_LEVEL_4:   legacyLevel = 4.0f; return true;
    case CodecLevel::H265_LEVEL_4_1: legacyLevel = 4.1f; return true;
    case CodecLevel::H265_LEVEL_5:   legacyLevel = 5.0f; return true;
    case CodecLevel::H265_LEVEL_5_1: legacyLevel = 5.1f; return true;
    case CodecLevel::H265_LEVEL_5_2: legacyLevel = 5.2f; return true;
    case CodecLevel::H265_LEVEL_6:   legacyLevel = 6.0f; return true;
    case CodecLevel::H265_LEVEL_6_1: legacyLevel = 6.1f; return true;
    case CodecLevel::H265_LEVEL_6_2: legacyLevel = 6.2f; return true;
    default:                         return false;
    }
}
}

int32_t ToHdrBit(const HdmiOutput::HDROutputMode mode)
{
    switch (mode) {
    case HdmiOutput::HDROutputMode::HLG:          return static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HLG);
    case HdmiOutput::HDROutputMode::HDR10:        return static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HDR10);
    case HdmiOutput::HDROutputMode::HDR10_PLUS:   return static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_HDR10PLUS);
    case HdmiOutput::HDROutputMode::DOLBY_VISION: return static_cast<int32_t>(HDRStandard::DS_HDRSTANDARD_DOLBYVISION);
    default:                                      return 0;
    }
}
}

dVideoDeviceAIDLImpl::dVideoDeviceAIDLImpl()
    : _opened(false)
{
    InitialiseHAL();
}

dVideoDeviceAIDLImpl::~dVideoDeviceAIDLImpl()
{
    DeInitialiseHAL();
}

// The HDMI output is opened, started and closed by HdmiOutputHelper, which is shared with the
// other HDMI Output components. If VideoDevice is the first to acquire the output it opens it;
// otherwise it attaches to the session that is already open, so a second open() is never issued.
bool dVideoDeviceAIDLImpl::AcquireOutput(const Output::Id& id, Device& device)
{
    HdmiOutputHelper::Callbacks callbacks;

    device.id = id;
    device.lease = HdmiOutputHelper::Instance().Acquire(id.value, std::move(callbacks));
    // VideoDevice only needs the output interface, so a lease without a controller is acceptable.
    if (!device.lease.Valid()) {
        DSLOG_ERR("HDMI output id=%d is not available to VideoDevice", id.value);
        return false;
    }
    device.output = device.lease.GetOutput();

    HdmiOutput::State state = HdmiOutput::State::UNKNOWN;
    if (device.output->getState(&state).isOk()) {
        DSLOG_INFO("VideoDevice acquired HDMI output id=%d, state=%d, status=%d",
            id.value, static_cast<int>(state), static_cast<int>(device.lease.GetStatus()));
    }
    return true;
}

dVideoDeviceAIDLImpl::Device* dVideoDeviceAIDLImpl::FindDevice(const int32_t handle)
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

#ifdef ENABLE_VIDEODECODER_AIDL
// Only getCapabilities() is used, which needs no open session, so the decoders are not opened and
// can be queried by any component. A missing service is not an error: the dummy capabilities are
// used instead.
void dVideoDeviceAIDLImpl::AcquireDecoders() // _adminLock held
{
    using DecoderManager = com::rdk::hal::videodecoder::IVideoDecoderManager;

    _decoders.clear();
    const android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    if (serviceManager == nullptr) {
        return;
    }
    const android::sp<android::IBinder> binder = serviceManager->checkService(
        android::String16(DecoderManager::serviceName().c_str()));
    const android::sp<DecoderManager> manager = binder == nullptr ? nullptr : android::interface_cast<DecoderManager>(binder);
    if (manager == nullptr) {
        DSLOG_ERR("VideoDecoder AIDL service is not available");
        return;
    }

    std::vector<Decoder::Id> ids;
    if (!manager->getVideoDecoderIds(&ids).isOk()) {
        DSLOG_ERR("getVideoDecoderIds failed");
        return;
    }
    for (const auto& id : ids) {
        android::sp<Decoder> decoder;
        if (manager->getVideoDecoder(id, &decoder).isOk() && decoder != nullptr) {
            _decoders.push_back(decoder);
        } else {
            DSLOG_ERR("getVideoDecoder failed for id=%d", id.value);
        }
    }
    DSLOG_INFO("VideoDecoder AIDL: %zu of %zu decoder(s) acquired", _decoders.size(), ids.size());
}
#endif

bool dVideoDeviceAIDLImpl::GetDecoderCapabilities(const Device& device, VideoDecoderCaps::Capabilities& caps)
{
    caps = VideoDecoderCaps::Capabilities();
#ifdef ENABLE_VIDEODECODER_AIDL
    // The decoder with the same index as the device is used; without one, all decoders are merged.
    if (device.decoder != nullptr) {
        if (!DecoderCaps::AppendAidlCapabilities(device.decoder, caps)) {
            DSLOG_ERR("IVideoDecoder.getCapabilities failed for handle=%d", device.id.value);
            return false;
        }
        return true;
    }
    if (!_decoders.empty()) {
        bool anyRead = false;
        for (const auto& decoder : _decoders) {
            anyRead |= DecoderCaps::AppendAidlCapabilities(decoder, caps);
        }
        if (!anyRead) {
            DSLOG_ERR("IVideoDecoder.getCapabilities failed for every decoder");
        }
        return anyRead;
    }
    DSLOG_ERR("No VideoDecoder is available for handle=%d", device.id.value);
#else
    (void)device;
    DSLOG_ERR("VideoDecoder AIDL support is not built");
#endif
    return false;
}

void dVideoDeviceAIDLImpl::InitialiseHAL()
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
        if (AcquireOutput(id, device)) {
            _devices.push_back(std::move(device));
        }
    }

    if (_devices.empty()) {
        DSLOG_ERR("VideoDevice AIDL initialisation FAILED: no HDMI output could be acquired");
        return;
    }
#ifdef ENABLE_VIDEODECODER_AIDL
    AcquireDecoders();
    for (size_t i = 0; i < _devices.size() && i < _decoders.size(); ++i) {
        _devices[i].decoder = _decoders[i];
    }
#endif
    getPersistenceValue();
    _opened = true;
    DSLOG_INFO("VideoDevice AIDL initialised: %zu of %zu HDMI output(s) acquired",
        _devices.size(), ids.size());
}

void dVideoDeviceAIDLImpl::DeInitialiseHAL()
{
    std::vector<Device> devices;
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        _opened = false;
        devices.swap(_devices);
#ifdef ENABLE_VIDEODECODER_AIDL
        _decoders.clear();
#endif
    }
    // Reset can wait for callbacks already in progress, so leases are released without
    // _adminLock held. The output closes only after the last component releases its lease.
    for (auto& device : devices) {
        device.lease.Reset();
    }
}

void dVideoDeviceAIDLImpl::setAllCallbacks(const CallbackBundle& bundle)
{
    std::lock_guard<std::mutex> lock(_adminLock);
    _callbacks = bundle;
}

void dVideoDeviceAIDLImpl::getPersistenceValue()
{
    // Keys are shared by all devices, as in the legacy implementation.
    VideoDeviceZoom zoom = VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL;
    bool forceDisableHDR = false;
    try {
        auto& persistence = device::HostPersistence::getInstance();
        if (persistence.getProperty("VideoDevice.DFC", "Full") == "None") {
            zoom = VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_NONE;
        }
        forceDisableHDR = persistence.getProperty("VideoDevice.forceHDRDisabled", "false") != "false";
    } catch (...) {
        DSLOG_INFO("Exception while reading VideoDevice persistence");
    }

    for (auto& device : _devices) {
        device.cachedZoom = zoom;
        device.forceDisableHDR = forceDisableHDR;
    }
}

uint32_t dVideoDeviceAIDLImpl::GetVideoDeviceHandle(const int32_t index, int32_t& handle)
{
    if (!_opened || index < 0 || static_cast<size_t>(index) >= _devices.size()) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    handle = _devices[index].id.value;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::SetVideoDeviceDFC(const int32_t handle, const VideoDeviceZoom zoomSetting)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    DSLOG_WARN("Zoom/DFC (ASPECT_RATIO) is a PlaneControl AIDL property, not exposed by IHDMIOutput; caching value only");
    device->cachedZoom = zoomSetting;
    try {
        device::HostPersistence::getInstance().persistHostProperty("VideoDevice.DFC",
            zoomSetting == VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_NONE ? "None" : "Full");
    } catch (...) {
        DSLOG_ERR("Failed to persist VideoDevice.DFC");
    }
    if (_callbacks.OnZoomSettingsChanged) {
        _callbacks.OnZoomSettingsChanged(zoomSetting);
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetVideoDeviceDFC(const int32_t handle, VideoDeviceZoom& zoomSetting)
{
    const Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    zoomSetting = device->cachedZoom;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetHDRCapabilities(const int32_t handle, int32_t& capabilities)
{
    const Device* device = FindDevice(handle);
    if (device == nullptr) {
        capabilities = 0x0; // dsHDRSTANDARD_NONE
        return WPEFramework::Core::ERROR_NONE;
    }
    HdmiOutput::Capabilities caps;
    if (!device->output->getCapabilities(&caps).isOk()) {
        capabilities = 0x0;
        return WPEFramework::Core::ERROR_NONE;
    }
    int32_t bitmask = 0x0;
    if (!device->forceDisableHDR) {
        int32_t outputBits = 0x0;
        for (const auto& mode : caps.supportedHDROutputModes) {
            outputBits |= ToHdrBit(mode);
        }
        // Mapping: IVideoDecoder.getCapabilities().supportedDynamicRanges and/or
        // IHDMIOutput.getCapabilities().supportedHDROutputModes. A format counts as supported when
        // the decoder handles it and the output can emit it; if the decoder capabilities cannot be
        // read, the HDMI output alone decides.
        VideoDecoderCaps::Capabilities decoderCaps;
        if (GetDecoderCapabilities(*device, decoderCaps)) {
            int32_t decoderBits = 0x0;
            for (const auto range : decoderCaps.supportedDynamicRanges) {
                decoderBits |= DecoderCaps::ToHdrBit(range);
            }
            bitmask = outputBits & decoderBits;
        } else {
            bitmask = outputBits;
        }
    }
    capabilities = bitmask;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetSupportedVideoCodingFormats(const int32_t handle, int32_t& supportedFormats)
{
    supportedFormats = 0x0;
    const Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_NONE;
    }
    // Mapping: IVideoDecoder.getCapabilities().supportedCodecs, converted from the codec list
    // to the legacy codec bitmask.
    VideoDecoderCaps::Capabilities decoderCaps;
    if (!GetDecoderCapabilities(*device, decoderCaps)) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    int32_t bitmask = 0x0;
    for (const auto& entry : decoderCaps.supportedCodecs) {
        VideoDeviceCodec legacyCodec;
        if (DecoderCaps::ToVideoDeviceCodec(entry.codec, legacyCodec)) {
            bitmask |= static_cast<int32_t>(legacyCodec);
        }
    }
    supportedFormats = bitmask;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo)
{
    codecInfo = nullptr;
    const Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    // Mapping: IVideoDecoder.getCapabilities().supportedCodecs entries, filtered by the requested
    // codec; each Profile entry becomes one legacy profile/level pair.
    VideoDecoderCaps::Capabilities decoderCaps;
    if (!GetDecoderCapabilities(*device, decoderCaps)) {
        return WPEFramework::Core::ERROR_GENERAL;
    }

    const VideoDecoderCaps::CodecCapabilities* codecCaps = nullptr;
    for (const auto& entry : decoderCaps.supportedCodecs) {
        VideoDeviceCodec legacyCodec;
        if (DecoderCaps::ToVideoDeviceCodec(entry.codec, legacyCodec) && legacyCodec == videoCodec) {
            codecCaps = &entry;
            break;
        }
    }
    if (codecCaps == nullptr) {
        DSLOG_WARN("Codec %d is not supported by the video decoder", static_cast<int>(videoCodec));
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }

    // Several decoder profiles can share one legacy profile (MAIN_10 and MAIN_10_HDR10): keep the highest level.
    std::vector<VideoDeviceCodecProfileSupport> profiles;
    for (const auto& decoderProfile : codecCaps->profiles) {
        VideoDeviceCodecProfileSupport support;
        if (!DecoderCaps::ToHevcProfile(decoderProfile.profile, support.profile)
            || !DecoderCaps::ToHevcLevel(decoderProfile.maxLevel, support.level)) {
            continue;
        }
        auto existing = std::find_if(profiles.begin(), profiles.end(),
            [&support](const VideoDeviceCodecProfileSupport& item) { return item.profile == support.profile; });
        if (existing == profiles.end()) {
            profiles.push_back(support);
        } else if (support.level > existing->level) {
            existing->level = support.level;
        }
    }

    using CodecProfileIterator = WPEFramework::RPC::IteratorType<IDeviceSettingsVideoCodecProfileSupportIterator>;
    codecInfo = WPEFramework::Core::Service<CodecProfileIterator>::Create<IDeviceSettingsVideoCodecProfileSupportIterator>(profiles);
    return codecInfo != nullptr ? WPEFramework::Core::ERROR_NONE : WPEFramework::Core::ERROR_GENERAL;
}

uint32_t dVideoDeviceAIDLImpl::DisableHDR(const int32_t handle, const bool disable)
{
    Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_NONE;
    }
    device->forceDisableHDR = disable;
    try {
        device::HostPersistence::getInstance().persistHostProperty("VideoDevice.forceHDRDisabled", disable ? "true" : "false");
    } catch (...) {
        DSLOG_ERR("Failed to persist VideoDevice.forceHDRDisabled");
    }
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::SetFRFMode(const int32_t handle, const int32_t frfmode)
{
    (void)handle;
    (void)frfmode;
    DSLOG_WARN("FRF mode is an IPanelOutput property, not exposed by IHDMIOutput");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoDeviceAIDLImpl::GetFRFMode(const int32_t handle, int32_t& frfmode)
{
    (void)handle;
    frfmode = 0;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoDeviceAIDLImpl::GetCurrentDisplayFrameRate(const int32_t handle, string& framerate)
{
    (void)handle;
    framerate.clear();
    DSLOG_WARN("Display frame rate is an IPanelOutput property (getRefreshRate) in the API mapping and is not supported here");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

uint32_t dVideoDeviceAIDLImpl::SetDisplayFrameRate(const int32_t handle, const string framerate)
{
    (void)handle;
    (void)framerate;
    DSLOG_WARN("Display frame rate is an IPanelOutput property (setRefreshRate) in the API mapping and is not supported here");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}
