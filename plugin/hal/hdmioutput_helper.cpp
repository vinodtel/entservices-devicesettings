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
#include "hdmioutput_helper.h"

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <com/rdk/hal/hdmioutput/BnHDMIOutputControllerListener.h>
#include <com/rdk/hal/hdmioutput/BnHDMIOutputEventListener.h>

#include <chrono>
#include <mutex>
#include <thread>

namespace {

template <typename Interface>
android::sp<Interface> GetService()
{
    const android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    if (serviceManager == nullptr) {
        return nullptr;
    }
    const android::sp<android::IBinder> binder = serviceManager->checkService(
        android::String16(Interface::serviceName().c_str()));
    return binder == nullptr ? nullptr : android::interface_cast<Interface>(binder);
}

// Identifies the subscriber whose callback is running on this thread, so that Reset() called from
// inside that callback does not wait for itself.
thread_local const void* t_inCallback = nullptr;

constexpr int kMaxOpenAttempts = 20;
constexpr std::chrono::milliseconds kOpenRetryWait(50);

} // namespace

// ---------------------------------------------------------------------------
// Listeners registered with the HDMI Output service. There is exactly one of each per output;
// events are fanned out to the components in HdmiOutputHelper.
// ---------------------------------------------------------------------------

class HdmiOutputHelper::SharedControllerListener : public com::rdk::hal::hdmioutput::BnHDMIOutputControllerListener {
public:
    SharedControllerListener(HdmiOutputHelper& helper, const int32_t id)
        : _helper(helper)
        , _id(id)
    {
    }

    android::binder::Status onHotPlugDetectStateChanged(bool state) override
    {
        _helper.OnHotPlug(_id, state);
        return android::binder::Status::ok();
    }

    android::binder::Status onFrameRateChanged() override
    {
        _helper.OnFrameRate(_id);
        return android::binder::Status::ok();
    }

    android::binder::Status onHDCPStatusChanged(HdmiOutputHelper::HdcpStatus status, HdmiOutputHelper::HdcpVersion version) override
    {
        _helper.OnHdcp(_id, status, version);
        return android::binder::Status::ok();
    }

    android::binder::Status onEDID(const std::vector<uint8_t>&) override
    {
        return android::binder::Status::ok();
    }

private:
    HdmiOutputHelper& _helper;
    const int32_t _id;
};

class HdmiOutputHelper::SharedEventListener : public com::rdk::hal::hdmioutput::BnHDMIOutputEventListener {
public:
    SharedEventListener(HdmiOutputHelper& helper, const int32_t id)
        : _helper(helper)
        , _id(id)
    {
    }

    android::binder::Status onStateChanged(HdmiOutputHelper::State oldState, HdmiOutputHelper::State newState) override
    {
        _helper.OnState(_id, oldState, newState);
        return android::binder::Status::ok();
    }

private:
    HdmiOutputHelper& _helper;
    const int32_t _id;
};

// ---------------------------------------------------------------------------
// Lease
// ---------------------------------------------------------------------------

HdmiOutputHelper::Lease::Lease(Lease&& other) noexcept
    : _id(other._id)
    , _token(other._token)
    , _status(other._status)
    , _output(std::move(other._output))
    , _controller(std::move(other._controller))
{
    other._id = -1;
    other._token = 0;
    other._status = AcquireStatus::UNAVAILABLE;
    other._output.clear();
    other._controller.clear();
}

HdmiOutputHelper::Lease& HdmiOutputHelper::Lease::operator=(Lease&& other) noexcept
{
    if (this != &other) {
        Reset();
        _id = other._id;
        _token = other._token;
        _status = other._status;
        _output = std::move(other._output);
        _controller = std::move(other._controller);
        other._id = -1;
        other._token = 0;
        other._status = AcquireStatus::UNAVAILABLE;
        other._output.clear();
        other._controller.clear();
    }
    return *this;
}

void HdmiOutputHelper::Lease::Reset()
{
    if (_token != 0) {
        const int32_t id = _id;
        const uint32_t token = _token;
        _token = 0;
        HdmiOutputHelper::Instance().Release(id, token);
    }
    _output.clear();
    _controller.clear();
    _status = AcquireStatus::UNAVAILABLE;
}

// ---------------------------------------------------------------------------
// HdmiOutputHelper
// ---------------------------------------------------------------------------

HdmiOutputHelper::~HdmiOutputHelper() = default;

HdmiOutputHelper& HdmiOutputHelper::Instance()
{
    // Intentionally never destroyed: no Binder calls may run during static destruction. If the
    // process exits while a session is open, the service closes it when this client dies.
    static HdmiOutputHelper* const instance = new HdmiOutputHelper();
    return *instance;
}

bool HdmiOutputHelper::EnsureManager() // _openLock held
{
    // Binder threads are required to receive the callbacks, including the hot-plug event that
    // is delivered while open() is in progress.
    static std::once_flag threadPoolOnce;
    std::call_once(threadPoolOnce, [] { android::ProcessState::self()->startThreadPool(); });

    if (_manager == nullptr) {
        _manager = GetService<Manager>();
    }
    if (_manager == nullptr) {
        DSLOG_ERR("HDMI Output AIDL service is not available");
        return false;
    }
    return true;
}

bool HdmiOutputHelper::GetOutputIds(std::vector<int32_t>& ids)
{
    ids.clear();
    std::lock_guard<std::mutex> open(_openLock);
    if (!EnsureManager()) {
        return false;
    }
    std::vector<Output::Id> outputIds;
    if (!_manager->getHDMIOutputIds(&outputIds).isOk()) {
        DSLOG_ERR("getHDMIOutputIds failed");
        return false;
    }
    for (const auto& outputId : outputIds) {
        ids.push_back(outputId.value);
    }
    return true;
}

HdmiOutputHelper::Entry* HdmiOutputHelper::FindEntry(const int32_t id)
{
    // Entries are only inserted while _openLock is held, so reading under _openLock is safe.
    auto it = _entries.find(id);
    return it == _entries.end() ? nullptr : &it->second;
}

HdmiOutputHelper::Entry* HdmiOutputHelper::GetOrCreateEntry(const int32_t id) // _openLock held
{
    if (Entry* existing = FindEntry(id)) {
        return existing;
    }

    Output::Id outputId;
    outputId.value = id;
    android::sp<Output> output;
    if (!_manager->getHDMIOutput(outputId, &output).isOk() || output == nullptr) {
        DSLOG_ERR("getHDMIOutput failed for id=%d", id);
        return nullptr;
    }

    std::lock_guard<std::mutex> state(_stateLock);
    Entry& entry = _entries[id];
    entry.id = id;
    entry.output = output;
    return &entry;
}

HdmiOutputHelper::AcquireStatus HdmiOutputHelper::OpenSession(Entry& e) // _openLock held
{
    for (int attempt = 0; attempt < kMaxOpenAttempts; ++attempt) {
        State state = State::UNKNOWN;
        const auto stateStatus = e.output->getState(&state);
        if (!stateStatus.isOk()) {
            DSLOG_ERR("getState() failed on HDMI output id=%d: %s", e.id, stateStatus.toString8().c_str());
            return AcquireStatus::UNAVAILABLE;
        }

        if (state == State::READY || state == State::STARTED) {
            // Held by someone this process does not know about. open() would only be rejected
            // with EX_ILLEGAL_STATE, so it is not attempted.
            DSLOG_WARN("HDMI output id=%d already open by another client (state=%d); output-only access",
                e.id, static_cast<int>(state));
            return AcquireStatus::OUTPUT_ONLY;
        }

        if (state != State::CLOSED) {
            // UNKNOWN, OPENING, STARTING, STOPPING or CLOSING: wait for it to settle.
            std::this_thread::sleep_for(kOpenRetryWait);
            continue;
        }

        if (e.controllerListener == nullptr) {
            android::sp<SharedControllerListener> listener = new SharedControllerListener(*this, e.id);
            e.controllerListener = listener;
        }
        if (e.eventListener == nullptr) {
            android::sp<SharedEventListener> listener = new SharedEventListener(*this, e.id);
            e.eventListener = listener;
        }

        android::sp<Controller> controller;
        const auto openStatus = e.output->open(e.controllerListener, &controller);
        if (!openStatus.isOk() || controller == nullptr) {
            // Another client may have won the race after the state was read: re-read and decide again.
            DSLOG_WARN("open() on HDMI output id=%d failed (attempt %d): %s",
                e.id, attempt + 1, openStatus.toString8().c_str());
            std::this_thread::sleep_for(kOpenRetryWait);
            continue;
        }

        if (!controller->start().isOk()) {
            DSLOG_ERR("HDMIOutputController::start() failed for id=%d", e.id);
            bool closed = false;
            e.output->close(controller, &closed); // roll back READY to CLOSED
            return AcquireStatus::UNAVAILABLE;
        }

        bool registered = false;
        e.eventListenerRegistered = e.output->registerEventListener(e.eventListener, &registered).isOk() && registered;
        if (!e.eventListenerRegistered) {
            DSLOG_ERR("registerEventListener failed on HDMI output id=%d", e.id);
        }

        {
            std::lock_guard<std::mutex> state(_stateLock);
            e.controller = controller;
        }
        DSLOG_INFO("HDMI output id=%d opened and started", e.id);
        return AcquireStatus::OPENED_BY_THIS_CALL;
    }

    DSLOG_ERR("HDMI output id=%d could not be opened", e.id);
    return AcquireStatus::UNAVAILABLE;
}

void HdmiOutputHelper::CloseSession(Entry& e) // _openLock held
{
    if (e.controller != nullptr) {
        // stop() needs STARTED and close() needs READY; other states raise EX_ILLEGAL_STATE.
        State state = State::STARTED;
        if (!e.output->getState(&state).isOk()) {
            state = State::STARTED;
        }
        if (state == State::STARTED) {
            e.controller->stop();
            state = State::READY;
        }
        if (state == State::READY) {
            bool closed = false;
            e.output->close(e.controller, &closed);
        }
    }
    if (e.eventListenerRegistered && e.eventListener != nullptr) {
        bool unregistered = false;
        e.output->unregisterEventListener(e.eventListener, &unregistered);
    }
    e.eventListenerRegistered = false;
    e.controllerListener.clear();
    e.eventListener.clear();

    std::lock_guard<std::mutex> state(_stateLock);
    e.controller.clear();
    e.hotPlug.reset();
    e.hdcp.reset();
    DSLOG_INFO("HDMI output id=%d closed", e.id);
}

HdmiOutputHelper::Lease HdmiOutputHelper::Acquire(const int32_t outputId, Callbacks callbacks)
{
    Lease lease;
    lease._id = outputId;

    std::shared_ptr<Subscriber> subscriber;
    {
        std::lock_guard<std::mutex> open(_openLock);

        if (!EnsureManager()) {
            return lease;
        }
        Entry* e = GetOrCreateEntry(outputId);
        if (e == nullptr) {
            return lease;
        }

        AcquireStatus status;
        if (e->controller != nullptr) {
            status = AcquireStatus::SHARED_EXISTING; // already open by this process: attach only
        } else {
            status = OpenSession(*e);
            if (status == AcquireStatus::UNAVAILABLE) {
                return lease;
            }
        }

        subscriber = std::make_shared<Subscriber>();
        subscriber->cb = std::move(callbacks);
        {
            std::lock_guard<std::mutex> state(_stateLock);
            lease._token = ++_nextToken;
            e->subs.emplace(lease._token, subscriber);
            e->refs++;
            lease._output = e->output;
            lease._controller = e->controller; // null for OUTPUT_ONLY
        }
        lease._status = status;
    }

    ReplayCachedState(outputId, *subscriber);
    return lease;
}

void HdmiOutputHelper::Release(const int32_t id, const uint32_t token)
{
    // 1. Detach the subscriber (short lock only).
    std::shared_ptr<Subscriber> subscriber;
    {
        std::lock_guard<std::mutex> state(_stateLock);
        auto entryIt = _entries.find(id);
        if (entryIt == _entries.end()) {
            return;
        }
        auto subIt = entryIt->second.subs.find(token);
        if (subIt == entryIt->second.subs.end()) {
            return;
        }
        subscriber = subIt->second;
        subscriber->active = false;
        entryIt->second.subs.erase(subIt);
    }

    // 2. Wait for a callback of this subscriber that is already running. No helper lock is held.
    //    Skipped when called from inside that very callback, which would otherwise wait for itself.
    if (t_inCallback != subscriber.get()) {
        std::lock_guard<std::mutex> drain(subscriber->callMutex);
    }

    // 3. The last reference stops and closes the session.
    std::lock_guard<std::mutex> open(_openLock);
    Entry* e = FindEntry(id);
    if (e == nullptr) {
        return;
    }
    bool last = false;
    {
        std::lock_guard<std::mutex> state(_stateLock);
        last = (--e->refs == 0);
    }
    if (last && e->controller != nullptr) {
        CloseSession(*e);
    }
}

void HdmiOutputHelper::ReplayCachedState(const int32_t id, Subscriber& subscriber)
{
    // The service reports hot-plug only while opening, so a component that attaches later must be
    // told the cached values. Not routed through callMutex on purpose: the lease has not been
    // returned yet so Reset() cannot run concurrently, and the caller may hold its own lock.
    std::optional<bool> hotPlug;
    std::optional<std::pair<HdcpStatus, HdcpVersion>> hdcp;
    {
        std::lock_guard<std::mutex> state(_stateLock);
        auto it = _entries.find(id);
        if (it == _entries.end()) {
            return;
        }
        hotPlug = it->second.hotPlug;
        hdcp = it->second.hdcp;
    }
    try {
        if (hotPlug.has_value() && subscriber.cb.onHotPlug) {
            subscriber.cb.onHotPlug(id, *hotPlug);
        }
        if (hdcp.has_value() && subscriber.cb.onHdcpStatus) {
            subscriber.cb.onHdcpStatus(id, hdcp->first, hdcp->second);
        }
    } catch (...) {
        DSLOG_ERR("exception in HDMI output replay callback (ignored)");
    }
}

std::vector<std::shared_ptr<HdmiOutputHelper::Subscriber>> HdmiOutputHelper::SnapshotSubscribers(const int32_t id)
{
    std::vector<std::shared_ptr<Subscriber>> targets;
    std::lock_guard<std::mutex> state(_stateLock);
    auto it = _entries.find(id);
    if (it != _entries.end()) {
        for (auto& kv : it->second.subs) {
            if (kv.second->active) {
                targets.push_back(kv.second);
            }
        }
    }
    return targets;
}

template <typename Fn>
void HdmiOutputHelper::Invoke(HdmiOutputHelper& helper, Subscriber& subscriber, Fn&& fn)
{
    std::lock_guard<std::mutex> call(subscriber.callMutex);
    {
        std::lock_guard<std::mutex> state(helper._stateLock);
        if (!subscriber.active) {
            return;
        }
    }
    const void* previous = t_inCallback;
    t_inCallback = &subscriber;
    try {
        fn();
    } catch (...) {
        DSLOG_ERR("exception in HDMI output callback (ignored)");
    }
    t_inCallback = previous;
}

void HdmiOutputHelper::OnHotPlug(const int32_t id, const bool connected)
{
    {
        std::lock_guard<std::mutex> state(_stateLock);
        auto it = _entries.find(id);
        if (it == _entries.end()) {
            return;
        }
        it->second.hotPlug = connected;
    }
    for (auto& subscriber : SnapshotSubscribers(id)) {
        Invoke(*this, *subscriber, [&] {
            if (subscriber->cb.onHotPlug) {
                subscriber->cb.onHotPlug(id, connected);
            }
        });
    }
}

void HdmiOutputHelper::OnFrameRate(const int32_t id)
{
    for (auto& subscriber : SnapshotSubscribers(id)) {
        Invoke(*this, *subscriber, [&] {
            if (subscriber->cb.onFrameRateChanged) {
                subscriber->cb.onFrameRateChanged(id);
            }
        });
    }
}

void HdmiOutputHelper::OnHdcp(const int32_t id, const HdcpStatus status, const HdcpVersion version)
{
    {
        std::lock_guard<std::mutex> state(_stateLock);
        auto it = _entries.find(id);
        if (it == _entries.end()) {
            return;
        }
        it->second.hdcp = std::make_pair(status, version);
    }
    for (auto& subscriber : SnapshotSubscribers(id)) {
        Invoke(*this, *subscriber, [&] {
            if (subscriber->cb.onHdcpStatus) {
                subscriber->cb.onHdcpStatus(id, status, version);
            }
        });
    }
}

void HdmiOutputHelper::OnState(const int32_t id, const State oldState, const State newState)
{
    for (auto& subscriber : SnapshotSubscribers(id)) {
        Invoke(*this, *subscriber, [&] {
            if (subscriber->cb.onStateChanged) {
                subscriber->cb.onStateChanged(id, oldState, newState);
            }
        });
    }
}
