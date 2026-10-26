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

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/PropertyValue.h>
#include <com/rdk/hal/hdmioutput/Capabilities.h>
#include <com/rdk/hal/hdmioutput/HDROutputMode.h>
#include <com/rdk/hal/hdmioutput/Property.h>
#include <com/rdk/hal/hdmioutput/State.h>
#include <com/rdk/hal/hdmioutput/VIC.h>

#include <cstdlib>

namespace HdmiOutput = com::rdk::hal::hdmioutput;
using PropertyValue = com::rdk::hal::PropertyValue;

namespace {
// Frame-rate-only view of the VIC table: which VIC to use to keep the same resolution
// family (1080p/2160p) while switching to a different frame rate (FRF-mode style switch).
struct FrameRateVicEntry {
    HdmiOutput::VIC vic;
    VideoResolution family;
    uint32_t frameRateHz;
};

const FrameRateVicEntry kFrameRateTable[] = {
    { HdmiOutput::VIC::VIC32_1920_1080_P_24_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 24 },
    { HdmiOutput::VIC::VIC33_1920_1080_P_25_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 25 },
    { HdmiOutput::VIC::VIC34_1920_1080_P_30_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 30 },
    { HdmiOutput::VIC::VIC31_1920_1080_P_50_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 50 },
    { HdmiOutput::VIC::VIC16_1920_1080_P_60_16_9, VideoResolution::DS_VIDEO_PIXELRES_1920X1080, 60 },
    { HdmiOutput::VIC::VIC93_3840_2160_P_24_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 24 },
    { HdmiOutput::VIC::VIC94_3840_2160_P_25_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 25 },
    { HdmiOutput::VIC::VIC95_3840_2160_P_30_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 30 },
    { HdmiOutput::VIC::VIC96_3840_2160_P_50_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 50 },
    { HdmiOutput::VIC::VIC97_3840_2160_P_60_16_9, VideoResolution::DS_VIDEO_PIXELRES_3840X2160, 60 },
};

const FrameRateVicEntry* FindByVic(HdmiOutput::VIC vic)
{
    for (const auto& entry : kFrameRateTable) {
        if (entry.vic == vic) {
            return &entry;
        }
    }
    return nullptr;
}

int32_t ExtractIntProperty(const std::optional<PropertyValue>& value, int32_t defaultValue)
{
    if (!value.has_value() || !value->value.has_value() || value->value->getTag() != PropertyValue::Value::intValue) {
        return defaultValue;
    }
    return value->value->get<PropertyValue::Value::intValue>();
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
    callbacks.onFrameRateChanged = [this](int32_t handle) {
        OnFrameRateChanged(handle);
    };

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
    }
    // The leases are released without _adminLock held: Reset() waits for callbacks that are already
    // running, and OnFrameRateChanged takes _adminLock. The output is closed only when the last
    // component using it releases its lease.
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
        for (const auto& mode : caps.supportedHDROutputModes) {
            switch (mode) {
            case HdmiOutput::HDROutputMode::HLG:          bitmask |= 0x02; break;
            case HdmiOutput::HDROutputMode::HDR10:        bitmask |= 0x01; break;
            case HdmiOutput::HDROutputMode::HDR10_PLUS:   bitmask |= 0x10; break;
            case HdmiOutput::HDROutputMode::DOLBY_VISION: bitmask |= 0x04; break;
            default: break;
            }
        }
    }
    capabilities = bitmask;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetSupportedVideoCodingFormats(const int32_t handle, int32_t& supportedFormats)
{
    (void)handle;
    // Codec support is a video-decoder capability (IVideoDecoderManager), not exposed by IHDMIOutput.
    supportedFormats = 0x0;
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo)
{
    (void)handle;
    (void)videoCodec;
    codecInfo = nullptr;
    return WPEFramework::Core::ERROR_UNAVAILABLE;
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
    const Device* device = FindDevice(handle);
    if (device == nullptr) {
        return WPEFramework::Core::ERROR_UNAVAILABLE;
    }
    std::optional<PropertyValue> value;
    if (!device->output->getProperty(HdmiOutput::Property::VIC, &value).isOk()) {
        return WPEFramework::Core::ERROR_GENERAL;
    }
    const auto vic = static_cast<HdmiOutput::VIC>(ExtractIntProperty(value, 0));
    const FrameRateVicEntry* entry = FindByVic(vic);
    framerate = entry != nullptr ? std::to_string(entry->frameRateHz) : "60";
    return WPEFramework::Core::ERROR_NONE;
}

uint32_t dVideoDeviceAIDLImpl::SetDisplayFrameRate(const int32_t handle, const string framerate)
{
    (void)handle;
    (void)framerate;
    DSLOG_WARN("Display frame rate is an IPanelOutput property (setRefreshRate) in the API mapping and is not supported here");
    return WPEFramework::Core::ERROR_UNAVAILABLE;
}

void dVideoDeviceAIDLImpl::OnFrameRateChanged(const int32_t handle)
{
    string framerate;
    if (GetCurrentDisplayFrameRate(handle, framerate) != WPEFramework::Core::ERROR_NONE) {
        return;
    }
    // Copied under the lock because setAllCallbacks() can replace the callbacks concurrently.
    std::function<void(const string)> postChange;
    {
        std::lock_guard<std::mutex> lock(_adminLock);
        postChange = _callbacks.OnDisplayFrameratePostChange;
    }
    if (postChange) {
        postChange(framerate);
    }
}
