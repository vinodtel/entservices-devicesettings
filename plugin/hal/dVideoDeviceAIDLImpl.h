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
#pragma once

#include "dVideoDevice.h"

#ifdef LOG_PRI
#undef LOG_PRI
#endif

#include <com/rdk/hal/hdmioutput/IHDMIOutput.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputController.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputManager.h>
#ifdef ENABLE_VIDEODECODER_AIDL
#include <com/rdk/hal/videodecoder/IVideoDecoder.h>
#include <com/rdk/hal/videodecoder/IVideoDecoderManager.h>
#endif

#include "hdmioutput_helper.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

// Decoder capabilities in a form that does not depend on the VideoDecoder AIDL headers. The types
// and values mirror com.rdk.hal.videodecoder (Capabilities, CodecCapabilities, Profile, Codec,
// DynamicRange, CodecProfile, CodecLevel); only the enumerators used by the legacy mapping are listed.
namespace VideoDecoderCaps {
enum class Codec : int32_t { MPEG2_VIDEO = 1, H264_AVC = 2, H265_HEVC = 3, VP9 = 4, AV1 = 5 };
enum class DynamicRange : int32_t { UNKNOWN = 0, SDR = 1, HLG = 2, HDR10 = 3, HDR10_PLUS = 4, DOLBY_VISION = 5 };
enum class CodecProfile : int32_t {
    MPEG2_MAIN = 2,
    H264_HIGH = 102,
    H265_MAIN = 200,
    H265_MAIN_10 = 201,
    H265_MAIN_10_HDR10 = 202,
};
enum class CodecLevel : int32_t {
    MPEG2_LEVEL_MAIN = 2,
    H264_LEVEL_4_1 = 103,
    H265_LEVEL_4 = 200,
    H265_LEVEL_4_1 = 201,
    H265_LEVEL_5 = 202,
    H265_LEVEL_5_1 = 203,
    H265_LEVEL_5_2 = 204,
    H265_LEVEL_6 = 205,
    H265_LEVEL_6_1 = 206,
    H265_LEVEL_6_2 = 207,
};

struct Profile {
    CodecProfile profile;
    CodecLevel maxLevel;
    int64_t maxBitrateInBps;
};

struct CodecCapabilities {
    Codec codec;
    std::vector<Profile> profiles;
};

struct Capabilities {
    std::vector<CodecCapabilities> supportedCodecs;
    std::vector<DynamicRange> supportedDynamicRanges;
};
}

// Implements the VideoDevice HAL Platform interface (see dVideoDevice.h) on top of the
// com.rdk.hal.hdmioutput AIDL HAL. Per the VideoPort/VideoDevice API mapping most dsVideoDevice.c
// APIs are backed by IPanelOutput/IVideoDecoderManager (not wired here). The HDMI Output surface
// (HDR capabilities, current VIC frame rate) is functional; the HDR capabilities, supported coding
// formats and codec info additionally use IVideoDecoder.getCapabilities(). That is read from the
// com.rdk.hal.videodecoder service when the plugin is built with ENABLE_VIDEODECODER_AIDL and the
// service is published; otherwise dummy decoder capabilities are used. The remaining APIs report
// ERROR_UNAVAILABLE.
//
// The HDMI output is shared with dVideoPortAIDLImpl through HdmiOutputHelper: IHDMIOutput.open()
// needs State::CLOSED and only one event listener is allowed per output, so whichever component
// acquires the output first opens it and the other attaches to the same session. The video decoders
// are only queried for their capabilities and are never opened, so they need no sharing.
class dVideoDeviceAIDLImpl : public hal::dVideoDevice::IPlatform {
public:
    dVideoDeviceAIDLImpl();
    ~dVideoDeviceAIDLImpl() override;

    dVideoDeviceAIDLImpl(const dVideoDeviceAIDLImpl&) = delete;
    dVideoDeviceAIDLImpl& operator=(const dVideoDeviceAIDLImpl&) = delete;

    void InitialiseHAL() override;
    void DeInitialiseHAL() override;
    void setAllCallbacks(const CallbackBundle& bundle) override;
    void getPersistenceValue() override;

    uint32_t GetVideoDeviceHandle(const int32_t index, int32_t& handle) override;
    uint32_t SetVideoDeviceDFC(const int32_t handle, const VideoDeviceZoom zoomSetting) override;
    uint32_t GetVideoDeviceDFC(const int32_t handle, VideoDeviceZoom& zoomSetting) override;
    uint32_t GetHDRCapabilities(const int32_t handle, int32_t& capabilities) override;
    uint32_t GetSupportedVideoCodingFormats(const int32_t handle, int32_t& supportedFormats) override;
    uint32_t GetCodecInfo(const int32_t handle, const VideoDeviceCodec videoCodec, IDeviceSettingsVideoCodecProfileSupportIterator*& codecInfo) override;
    uint32_t DisableHDR(const int32_t handle, const bool disable) override;
    uint32_t SetFRFMode(const int32_t handle, const int32_t frfmode) override;
    uint32_t GetFRFMode(const int32_t handle, int32_t& frfmode) override;
    uint32_t GetCurrentDisplayFrameRate(const int32_t handle, string& framerate) override;
    uint32_t SetDisplayFrameRate(const int32_t handle, const string framerate) override;

private:
    using Output = com::rdk::hal::hdmioutput::IHDMIOutput;
#ifdef ENABLE_VIDEODECODER_AIDL
    using Decoder = com::rdk::hal::videodecoder::IVideoDecoder;
#endif

    // Handle exposed to callers is the HDMI output Id value.
    struct Device {
        Output::Id id;
        HdmiOutputHelper::Lease lease;
        android::sp<Output> output; // copy of lease output: getCapabilities/getProperty/getState
#ifdef ENABLE_VIDEODECODER_AIDL
        android::sp<Decoder> decoder; // decoder with the same index as this device, null if there is none
#endif
        VideoDeviceZoom cachedZoom = VideoDeviceZoom::DS_VIDEO_DEVICE_ZOOM_FULL;
        bool forceDisableHDR = true;
    };

    bool AcquireOutput(const Output::Id& id, Device& device);
    Device* FindDevice(const int32_t handle);

    // Fills `caps` from IVideoDecoder.getCapabilities(); falls back to dummy data when the
    // VideoDecoder service is not available. Returns false only if a reachable decoder fails.
    bool GetDecoderCapabilities(const Device& device, VideoDecoderCaps::Capabilities& caps);
#ifdef ENABLE_VIDEODECODER_AIDL
    void AcquireDecoders();
#endif

    void OnFrameRateChanged(const int32_t handle);

    std::mutex _adminLock;
    CallbackBundle _callbacks;
    std::vector<Device> _devices;
#ifdef ENABLE_VIDEODECODER_AIDL
    std::vector<android::sp<Decoder>> _decoders;
#endif
    std::atomic<bool> _opened;
};
