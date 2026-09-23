/**
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
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
#include <iostream>
#include <string>

#include "plugins/ets/tests/ani/ani_gtest/ani_gtest.h"

#include "plugins/ets/runtime/ets_coroutine.h"

#include "plugins/ets/runtime/ets_vm.h"
#include "plugins/ets/runtime/integrate/ark_vm_api.h"
#include "runtime/include/thread_scopes.h"
#include "plugins/ets/runtime/types/ets_error.h"

#include "runtime/include/class.h"
#include "runtime/include/object_header.h"
#include "runtime/execution/coroutines/stackful/stackful_coroutine_manager.h"

namespace ark::ets::test {

namespace {

constexpr size_t SIGQUIT_LOG_PAYLOAD_SIZE = 768U;

}  // namespace

class EtsCoroutineDFXTest : public ani::testing::AniTest {
public:
    static ani_function ResolveFunction(ani_env *env, std::string_view methodName, std::string_view signature)
    {
        ani_module md;
        [[maybe_unused]] auto status = env->FindModule("coroutine_dfx_test", &md);
        ASSERT(status == ANI_OK);
        ani_function func;
        status = env->Module_FindFunction(md, methodName.data(), signature.data(), &func);
        ASSERT(status == ANI_OK);
        return func;
    }

    StackfulCoroutineManager *GetCoroutineManager() const
    {
        auto *coroutine = EtsCoroutine::GetCurrent();
        auto *etsVm = coroutine->GetPandaVM();
        return static_cast<StackfulCoroutineManager *>(etsVm->GetJobManager());
    }

private:
    std::vector<ani_option> GetExtraAniOptions() override
    {
        ani_option gcType = {"--ext:gc-type=epsilon", nullptr};
        ani_option jit = {"--ext:compiler-enable-jit=false", nullptr};
        // Use coroutine-workers-count = 1 to guaranty that after schedule
        ani_option coroutineWorkersCount = {"--ext:coroutine-workers-count=1", nullptr};
        return std::vector<ani_option> {gcType, jit, coroutineWorkersCount};
    }
};

TEST_F(EtsCoroutineDFXTest, PrintStackTest)
{
    auto *coroutineManager = GetCoroutineManager();

    CallEtsFunction<ani_int>("coroutine_dfx_test", "startWaitCoro");
    coroutineManager->ExecuteJobs();
    auto snapshot = coroutineManager->GetAllWorkerFullStatus();
    CallEtsFunction<ani_int>("coroutine_dfx_test", "notify");
    coroutineManager->ExecuteJobs();

    auto coroInfo = snapshot->OutputInfo();
    ASSERT_TRUE(coroInfo.find("Status: BLOCKED") != coroInfo.npos);
    ASSERT_TRUE(coroInfo.find("coroutine_dfx_test.ETSGLOBAL::%%async-waiter") !=
                coroInfo.npos);  // check if it contains stack with waiter()
    ASSERT_EQ(coroInfo.find("WaitReason:"), coroInfo.npos);
}

TEST_F(EtsCoroutineDFXTest, MutexWaitSnapshotTest)
{
    auto *coroutineManager = GetCoroutineManager();
    CallEtsFunction<ani_int>("coroutine_dfx_test", "startMutexWaitCoro");
    coroutineManager->ExecuteJobs();

    auto coroInfo = coroutineManager->GetAllWorkerFullStatus()->OutputInfo();
    ASSERT_NE(coroInfo.find("WaitReason: MUTEX_LOCK"), coroInfo.npos);
    ASSERT_NE(coroInfo.find("WaitMode: MUTEX"), coroInfo.npos);
    ASSERT_EQ(coroInfo.find("WaitResource:"), coroInfo.npos);

    ASSERT_EQ(CallEtsFunction<ani_int>("coroutine_dfx_test", "dumpContainsMutexWait"), 1);

    auto *etsVm = EtsCoroutine::GetCurrent()->GetPandaVM();
    etsVm->DumpForSigQuit(std::cerr);

    CallEtsFunction<ani_int>("coroutine_dfx_test", "unlockMutex");
    coroutineManager->ExecuteJobs();
}

TEST_F(EtsCoroutineDFXTest, CoroutineDumpIsCompleteTest)
{
    auto *coroutineManager = GetCoroutineManager();
    CallEtsFunction<ani_int>("coroutine_dfx_test", "startManyMutexWaitCoroutines");
    coroutineManager->ExecuteJobs();

    auto *etsVm = EtsCoroutine::GetCurrent()->GetPandaVM();
    auto coroutineInfo = etsVm->GetCoroutineInfo();
    ASSERT_GT(coroutineInfo.size(), SIGQUIT_LOG_PAYLOAD_SIZE);
    ASSERT_EQ(CallEtsFunction<ani_int>("coroutine_dfx_test", "dumpContainsAllMutexWaiters"), 1);

    etsVm->DumpForSigQuit(std::cerr);

    CallEtsFunction<ani_int>("coroutine_dfx_test", "unlockMutex");
    coroutineManager->ExecuteJobs();
}

TEST_F(EtsCoroutineDFXTest, RWLockWriteWaitSnapshotTest)
{
    auto *coroutineManager = GetCoroutineManager();
    CallEtsFunction<ani_int>("coroutine_dfx_test", "startRWWriteWaitCoro");
    coroutineManager->ExecuteJobs();

    auto coroInfo = coroutineManager->GetAllWorkerFullStatus()->OutputInfo();
    ASSERT_NE(coroInfo.find("WaitReason: RWLOCK_WRITE"), coroInfo.npos);
    ASSERT_NE(coroInfo.find("WaitMode: WRITELOCK"), coroInfo.npos);
    ASSERT_NE(coroInfo.find("Readers: 1"), coroInfo.npos);
    ASSERT_NE(coroInfo.find("Writers: 1"), coroInfo.npos);
    ASSERT_EQ(coroInfo.find("WaitResource:"), coroInfo.npos);
    ASSERT_EQ(CallEtsFunction<ani_int>("coroutine_dfx_test", "dumpContainsRWWriteWait"), 1);

    CallEtsFunction<ani_int>("coroutine_dfx_test", "unlockRWRead");
    coroutineManager->ExecuteJobs();
}

TEST_F(EtsCoroutineDFXTest, RWLockReadWaitSnapshotTest)
{
    auto *coroutineManager = GetCoroutineManager();
    CallEtsFunction<ani_int>("coroutine_dfx_test", "startRWReadWaitCoro");
    coroutineManager->ExecuteJobs();

    auto coroInfo = coroutineManager->GetAllWorkerFullStatus()->OutputInfo();
    ASSERT_NE(coroInfo.find("WaitReason: RWLOCK_READ"), coroInfo.npos);
    ASSERT_NE(coroInfo.find("WaitMode: READLOCK"), coroInfo.npos);
    ASSERT_NE(coroInfo.find("Readers: 1"), coroInfo.npos);
    ASSERT_NE(coroInfo.find("Writers: 1"), coroInfo.npos);
    ASSERT_EQ(coroInfo.find("WaitResource:"), coroInfo.npos);
    ASSERT_EQ(CallEtsFunction<ani_int>("coroutine_dfx_test", "dumpContainsRWReadWait"), 1);

    CallEtsFunction<ani_int>("coroutine_dfx_test", "unlockRWWrite");
    coroutineManager->ExecuteJobs();
}

namespace {
void CheckCurrentStackRange(void **stackAddr)
{
    size_t stackSize = 0;
    ASSERT_EQ(ARKVM_GetCoroutineStackRange(stackAddr, &stackSize), ARKVM_OK);
    ASSERT_NE(*stackAddr, nullptr);
    ASSERT_GT(stackSize, 0U);
    auto low = reinterpret_cast<uintptr_t>(*stackAddr);
    auto local = reinterpret_cast<uintptr_t>(&stackSize);
    ASSERT_GE(local, low);
    ASSERT_LT(local - low, stackSize);
}
}  // namespace

TEST_F(EtsCoroutineDFXTest, RejectsNullOutputs)
{
    void *addr = nullptr;
    size_t size = 0;
    EXPECT_EQ(ARKVM_GetCoroutineStackRange(nullptr, &size), ARKVM_INVALID_ARGS);
    EXPECT_EQ(ARKVM_GetCoroutineStackRange(&addr, nullptr), ARKVM_INVALID_ARGS);
    EXPECT_EQ(ARKVM_GetCoroutineStackRange(nullptr, nullptr), ARKVM_INVALID_ARGS);
}

TEST_F(EtsCoroutineDFXTest, RejectsUnattachedThreadAndClearsOutputs)
{
    std::thread thread([] {
        size_t size = 1;
        void *addr = &size;
        EXPECT_EQ(ARKVM_GetCoroutineStackRange(&addr, &size), ARKVM_INVALID_CONTEXT);
        EXPECT_EQ(addr, nullptr);
        EXPECT_EQ(size, 0U);
    });
    thread.join();
}

TEST_F(EtsCoroutineDFXTest, MainCoroutineStackContainsLocalVariable)
{
    void *addr = nullptr;
    CheckCurrentStackRange(&addr);
}

TEST_F(EtsCoroutineDFXTest, LaunchedCoroutineHasItsOwnStack)
{
    void *mainAddr = nullptr;
    CheckCurrentStackRange(&mainAddr);
    void *childAddr = nullptr;
    auto *manager = JobExecutionContext::GetCurrent()->GetManager();
    auto *job = manager->CreateJob(
        PandaString {"stack-range-test"},
        Job::NativeEntrypointInfo {[](void *data) { CheckCurrentStackRange(static_cast<void **>(data)); }, &childAddr});
    ASSERT_NE(job, nullptr);
    LaunchParams params(true);
    {
        ScopedManagedCodeThread managedScope(ManagedThread::GetCurrent());
        ASSERT_EQ(manager->Launch(job, params), LaunchResult::OK);
    }
    ASSERT_NE(childAddr, nullptr);
    EXPECT_NE(childAddr, mainAddr);
    void *resumedAddr = nullptr;
    CheckCurrentStackRange(&resumedAddr);
    EXPECT_EQ(resumedAddr, mainAddr);
}
}  // namespace ark::ets::test
