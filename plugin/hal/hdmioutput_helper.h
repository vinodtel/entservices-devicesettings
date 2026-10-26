/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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

#include "DeviceSettingsTypes.h"

#ifdef LOG_PRI
#undef LOG_PRI
#endif

#include <com/rdk/hal/hdmioutput/Capabilities.h>
#include <com/rdk/hal/hdmioutput/HDCPProtocolVersion.h>
#include <com/rdk/hal/hdmioutput/HDCPStatus.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutput.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputController.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputControllerListener.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputEventListener.h>
#include <com/rdk/hal/hdmioutput/IHDMIOutputManager.h>
#include <com/rdk/hal/hdmioutput/PlatformCapabilities.h>
#include <com/rdk/hal/hdmioutput/State.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

// Process-wide owner of the HDMI Output AIDL sessions.
//
// IHDMIOutput.open() needs State::CLOSED and only one controller listener and one event
// listener can be registered per output, so two components must never open the same output
// independently. Components call Acquire(): the first caller opens and starts the output,
// later callers for the same id attach to the already open session. The session is stopped
// and closed when the last lease is released.
//
// Must be built into a single shared object (it is implemented in hdmioutput_helper.cpp) so
// that exactly one instance exists per process.
class HdmiOutputHelper {
public:
    using Manager = com::rdk::hal::hdmioutput::IHDMIOutputManager;
    using Output = com::rdk::hal::hdmioutput::IHDMIOutput;
    using Controller = com::rdk::hal::hdmioutput::IHDMIOutputController;
    using State = com::rdk::hal::hdmioutput::State;
    using HdcpStatus = com::rdk::hal::hdmioutput::HDCPStatus;
    using HdcpVersion = com::rdk::hal::hdmioutput::HDCPProtocolVersion;

    enum class AcquireStatus {
        OPENED_BY_THIS_CALL, // this call performed open() and start()
        SHARED_EXISTING,     // attached to the session this process already owns
        OUTPUT_ONLY,         // output is held by someone else: output is usable, controller is null
        UNAVAILABLE          // service or output not reachable: the lease is empty
    };

    // All members are optional and are invoked on a Binder thread.
    // They must not block and must not call Acquire() or Lease::Reset().
    struct Callbacks {
        std::function<void(int32_t id, bool connected)> onHotPlug;
        std::function<void(int32_t id)> onFrameRateChanged;
        std::function<void(int32_t id, HdcpStatus status, HdcpVersion version)> onHdcpStatus;
        std::function<void(int32_t id, State oldState, State newState)> onStateChanged;
    };

    // Move-only reference to a shared output. The output and controller stay valid while the
    // lease is held, because the session cannot be closed until the last lease is released.
    class Lease {
    public:
        Lease() = default;
        ~Lease() { Reset(); }
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        bool Valid() const { return _output != nullptr; }
        bool HasController() const { return _controller != nullptr; }
        AcquireStatus GetStatus() const { return _status; }
        android::sp<Output> GetOutput() const { return _output; }
        android::sp<Controller> GetController() const { return _controller; } // null for OUTPUT_ONLY

        // Drops the reference and waits for any callback of this lease that is already running.
        // Safe to call more than once. Must not be called while holding a lock that a callback
        // of this lease can take.
        void Reset();

    private:
        friend class HdmiOutputHelper;
        int32_t _id = -1;
        uint32_t _token = 0; // 0 means the lease holds no reference
        AcquireStatus _status = AcquireStatus::UNAVAILABLE;
        android::sp<Output> _output;
        android::sp<Controller> _controller;
    };

    static HdmiOutputHelper& Instance();

    // Ids reported by the HDMI Output service.
    bool GetOutputIds(std::vector<int32_t>& ids);

    // Capabilities are constant and independent of the output state, so these do not open the output.
    bool GetOutputCapabilities(int32_t outputId, com::rdk::hal::hdmioutput::Capabilities& capabilities);
    bool GetPlatformCapabilities(com::rdk::hal::hdmioutput::PlatformCapabilities& capabilities);

    // The first caller for an id opens the output; later callers share it. Never throws.
    Lease Acquire(int32_t outputId, Callbacks callbacks);

    HdmiOutputHelper(const HdmiOutputHelper&) = delete;
    HdmiOutputHelper& operator=(const HdmiOutputHelper&) = delete;

private:
    HdmiOutputHelper() = default;
    ~HdmiOutputHelper();

    class SharedControllerListener;
    class SharedEventListener;

    struct Subscriber {
        Callbacks cb;
        std::mutex callMutex; // held while one of this subscriber's callbacks runs
        bool active = true;   // guarded by _stateLock
    };

    // One per output id. Entries are never erased, so references to them stay valid.
    struct Entry {
        int32_t id = -1;
        android::sp<Output> output;
        android::sp<Controller> controller; // non-null only while this process owns the session
        android::sp<com::rdk::hal::hdmioutput::IHDMIOutputControllerListener> controllerListener;
        android::sp<com::rdk::hal::hdmioutput::IHDMIOutputEventListener> eventListener;
        bool eventListenerRegistered = false;
        int refs = 0;
        std::optional<bool> hotPlug; // cached: the service reports it only while opening
        std::optional<std::pair<HdcpStatus, HdcpVersion>> hdcp;
        std::map<uint32_t, std::shared_ptr<Subscriber>> subs;
    };

    bool EnsureManager();
    Entry* GetOrCreateEntry(int32_t id);
    Entry* FindEntry(int32_t id);
    AcquireStatus OpenSession(Entry& entry);
    void CloseSession(Entry& entry);
    void Release(int32_t id, uint32_t token);
    void ReplayCachedState(int32_t id, Subscriber& subscriber);

    void OnHotPlug(int32_t id, bool connected);
    void OnFrameRate(int32_t id);
    void OnHdcp(int32_t id, HdcpStatus status, HdcpVersion version);
    void OnState(int32_t id, State oldState, State newState);

    template <typename Fn>
    static void Invoke(HdmiOutputHelper& helper, Subscriber& subscriber, Fn&& fn);
    std::vector<std::shared_ptr<Subscriber>> SnapshotSubscribers(int32_t id);

    std::mutex _openLock;  // open/close/attach decisions; may be held across Binder calls
    std::mutex _stateLock; // entries, caches, subscriber lists; never held across Binder calls or callbacks
    // Lock order: _openLock, then _stateLock. Binder callbacks only take _stateLock.

    android::sp<Manager> _manager;       // guarded by _openLock
    std::map<int32_t, Entry> _entries;   // structure guarded by _openLock and _stateLock
    uint32_t _nextToken = 0;             // guarded by _stateLock
};
