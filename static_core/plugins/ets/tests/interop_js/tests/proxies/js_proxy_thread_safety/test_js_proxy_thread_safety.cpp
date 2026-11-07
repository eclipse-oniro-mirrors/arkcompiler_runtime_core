/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "ets_interop_js_gtest.h"
#include "libarkbase/os/mutex.h"
#include "plugins/ets/runtime/ets_platform_types.h"
#include "plugins/ets/runtime/interop_js/js_proxy/js_proxy.h"
#include "runtime/include/runtime_notification.h"
#include "runtime/include/thread_scopes.h"
#include "runtime/class_linker_context.h"

namespace ark::ets::interop::js::testing {

class JSProxyThreadSafetyTest : public EtsInteropTest {
public:
    // CC-OFFNXT(G.NAM.03-CPP) project code style
    static constexpr size_t THREADS_COUNT = 16U;

    JSProxyThreadSafetyTest()
    {
        ani_vm *aniVm = nullptr;
        ani_size vmCount = 0;
        ANI_GetCreatedVMs(&aniVm, 1U, &vmCount);
        ASSERT(vmCount == 1U);
        vm_ = aniVm;
    }

protected:
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    ani_vm *vm_ {nullptr};
    // NOLINTEND(misc-non-private-member-variables-in-classes)
};

namespace {

struct ProxyCreationResult {
    bool attachOk {false};
    bool proxyOk {false};
    Class *cls {nullptr};
    bool clsInitialized {false};
};

class ProxyClassParkListener : public RuntimeListener {
public:
    explicit ProxyClassParkListener(const uint8_t *descriptor) : descriptor_(descriptor) {}

    bool WaitForParkedClass(std::chrono::seconds timeout, Class **parkedClass)
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        // Atomic with acquire order reason: sync with the builder's store before reading the class.
        while (parkedClass_.load(std::memory_order_acquire) == nullptr) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
        }
        // Atomic with acquire order reason: sync with the builder's store before reading the class.
        *parkedClass = parkedClass_.load(std::memory_order_acquire);
        return true;
    }

    void Release()
    {
        {
            os::memory::LockHolder lock(parkMutex_);
            released_ = true;
        }
        parkCv_.SignalAll();
    }

protected:
    void ClassPrepare(Class *klass) override
    {
        // Atomic with acquire order reason: check whether the park point was already reached.
        if (parkedClass_.load(std::memory_order_acquire) != nullptr) {
            return;
        }
        if (!js_proxy::JSProxy::IsProxyClass(klass) || !utf::IsEqual(klass->GetDescriptor(), descriptor_)) {
            return;
        }

        // Atomic with release order reason: publish the parked class before the main thread reads it.
        parkedClass_.store(klass, std::memory_order_release);

        ScopedNativeCodeThread native(ManagedThread::GetCurrent());
        os::memory::LockHolder lock(parkMutex_);
        while (!released_) {
            parkCv_.Wait(&parkMutex_);
        }
    }

private:
    const uint8_t *descriptor_;
    std::atomic<Class *> parkedClass_ {nullptr};
    bool released_ {false};
    os::memory::Mutex parkMutex_;
    os::memory::ConditionVariable parkCv_;
};

}  // namespace

TEST_F(JSProxyThreadSafetyTest, concurrent_interface_proxy_creation)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    auto *iface = PlatformTypes(executionCtx)->coreFunctions[1U]->GetRuntimeClass();
    PandaSet<Class *> interfaces;
    interfaces.insert(iface);
    std::string interfaceName = utf::Mutf8AsCString(iface->GetDescriptor());

    std::string descriptorStr = "L$" + interfaceName.substr(1U, interfaceName.size() - 2U) + "$;";
    auto *descriptor = utf::CStringAsMutf8(descriptorStr.c_str());

    auto *context = PlatformTypes(executionCtx)->coreObject->GetRuntimeClass()->GetLoadContext();
    ASSERT_EQ(context->FindClass(descriptor), nullptr);

    ProxyClassParkListener listener(descriptor);
    Runtime::GetCurrent()->GetNotificationManager()->AddListener(&listener,
                                                                 RuntimeNotificationManager::Event::CLASS_EVENTS);

    std::atomic_size_t readyCount {0U};
    std::atomic_size_t finishedCount {0U};
    std::atomic_size_t failures {0U};
    std::atomic_size_t nonNullProxies {0U};

    std::vector<ProxyCreationResult> results(THREADS_COUNT);
    std::vector<std::thread> threads;
    threads.reserve(THREADS_COUNT);

    auto threadBody = [this, &interfaces, &interfaceName, &readyCount, &finishedCount, &failures, &nonNullProxies,
                       &results](size_t idx) {
        ani_env *aniEnv = nullptr;
        ani_option opt {"--interop=disable", nullptr};
        ani_options opts {1U, &opt};
        if (vm_->AttachCurrentThread(&opts, ANI_VERSION_1, &aniEnv) != ANI_OK) {
            // Atomic with relaxed order reason: failure counting only, no ordering is required.
            failures.fetch_add(1U, std::memory_order_relaxed);
            // Atomic with release order reason: unblock the start barrier for the others even on the failure path.
            readyCount.fetch_add(1U, std::memory_order_release);
            // Atomic with release order reason: publish this thread's completion even on the failure path.
            finishedCount.fetch_add(1U, std::memory_order_release);
            return;
        }
        // Atomic with release order reason: publish this thread's attach effects before charging together.
        readyCount.fetch_add(1U, std::memory_order_release);
        // Atomic with acquire order reason: see the attach effects of every other thread before the race.
        while (readyCount.load(std::memory_order_acquire) < THREADS_COUNT) {
        }
        {
            ScopedManagedCodeThread managedCode(EtsExecutionContext::GetCurrent()->GetMT());
            auto *proxy = js_proxy::JSProxy::CreateInterfaceProxy(interfaces, interfaceName);
            if (proxy == nullptr) {
                // Atomic with relaxed order reason: failure counting only, no ordering is required.
                failures.fetch_add(1U, std::memory_order_relaxed);
            } else {
                Class *proxyCls = proxy->GetProxyClass()->GetRuntimeClass();
                results[idx] = ProxyCreationResult {true, true, proxyCls, proxyCls->IsInitialized()};
                // Atomic with relaxed order reason: success counting only, checked after the join barrier.
                nonNullProxies.fetch_add(1U, std::memory_order_relaxed);
                Runtime::GetCurrent()->GetInternalAllocator()->Delete(proxy);
            }
        }
        [[maybe_unused]] ani_status detachStatus = vm_->DetachCurrentThread();
        // Atomic with release order reason: publish this thread's result before the main thread proceeds.
        finishedCount.fetch_add(1U, std::memory_order_release);
        ASSERT(detachStatus == ANI_OK);
    };

    for (size_t i = 0; i < THREADS_COUNT; i++) {
        threads.emplace_back(threadBody, i);
    }

    Class *parkedCls = nullptr;
    ASSERT_TRUE(listener.WaitForParkedClass(std::chrono::seconds(30U), &parkedCls))
        << "no thread reached the ClassPrepare park point";

    // Atomic with acquire order reason: see the racing threads' results before releasing the winner.
    while (finishedCount.load(std::memory_order_acquire) < THREADS_COUNT - 1U) {
    }

    listener.Release();
    for (auto &t : threads) {
        t.join();
    }

    // Atomic with relaxed order reason: read after the join barrier, no extra ordering is required.
    ASSERT_EQ(failures.load(std::memory_order_relaxed), 0U);
    // Atomic with relaxed order reason: read after the join barrier, no extra ordering is required.
    ASSERT_EQ(nonNullProxies.load(std::memory_order_relaxed), THREADS_COUNT);
    for (const auto &result : results) {
        ASSERT_EQ(result.cls, parkedCls) << "racing threads saw different proxy classes";
        ASSERT_TRUE(result.clsInitialized) << "a racing thread got an uninitialized class";
    }
    ASSERT_TRUE(parkedCls->IsInitialized());
    ASSERT_TRUE(parkedCls->IsXRefClass());

    Runtime::GetCurrent()->GetNotificationManager()->RemoveListener(&listener,
                                                                    RuntimeNotificationManager::Event::CLASS_EVENTS);
}

TEST_F(JSProxyThreadSafetyTest, concurrent_js_object_to_interface_proxy)
{
    auto res = CallEtsFunction<bool>("js_proxy_thread_safety", "jsProxyThreadSafetyTest");
    if (!res.has_value()) {
        // Clear the pending exception so it does not leak into the next test's SetUp.
        napi_env env = GetJsEnv();
        bool hasPendingExc = false;
        [[maybe_unused]] napi_status status = napi_is_exception_pending(env, &hasPendingExc);
        if (hasPendingExc) {
            napi_value exp;
            napi_get_and_clear_last_exception(env, &exp);
        }
        FAIL() << "CallEtsFunction(jsProxyThreadSafetyTest) returned nullopt";
    }
    ASSERT_EQ(true, res.value());
}

}  // namespace ark::ets::interop::js::testing
