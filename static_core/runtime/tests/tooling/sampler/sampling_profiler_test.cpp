/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <atomic>

#include "assembler/assembly-parser.h"
#include "libarkfile/file.h"
#include "libarkbase/trace/trace.h"
#include "libarkbase/panda_gen_options/generated/logger_options.h"
#include "libarkbase/utils/type_helpers.h"
#include "runtime/include/thread_scopes.h"
#include "runtime/include/runtime.h"
#include "runtime/tooling/sampler/sampling_profiler.h"
#include "runtime/interpreter/runtime_interface.h"
#include "tools/sampler/aspt_converter.h"

namespace ark::tooling::sampler::test {

inline std::string Separator()
{
#ifdef _WIN32
    return "\\";
#else
    return "/";
#endif
}

static const char *g_profilerFilename = "profiler_result.aspt";
static const char *g_pandaFileName = "sampling_profiler_test_ark_asm.abc";
static constexpr size_t TEST_CYCLE_THRESHOLD = 100;
static const std::shared_ptr<SamplesRecord> G_SAMPLES_RECORD = std::make_shared<SamplesRecord>();

const uint32_t BC_OFFSET_10 = 10;
const int32_t FILE_ID_100 = 100;
const int32_t FILE_ID_200 = 200;
const int32_t NUMBER_2 = 2;
const int32_t NUMBER_3 = 3;
const int32_t NUMBER_4 = 4;
const int32_t NUMBER_5 = 5;
const int32_t NUMBER_10 = 10;

inline std::string GetPandaFilePath()
{
    auto execPath = ark::os::file::File::GetExecutablePath();
    return execPath.Value() + Separator() + ".." + Separator() + "runtime" + Separator() + "tests" + Separator() +
           "tooling" + Separator() + "sampler" + Separator() + g_pandaFileName;
}

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
class SamplerTest : public testing::Test {
public:
    // NOLINTNEXTLINE(readability-function-size)
    void SetUp() override
    {
        Logger::Initialize(logger::Options(""));

        RuntimeOptions options;
        options.SetLoadRuntimes({"core"});
        options.SetRunGcInPlace(true);
        options.SetVerifyCallStack(false);
        options.SetInterpreterType("cpp");
        auto execPath = ark::os::file::File::GetExecutablePath();
        std::string pandaStdLib =
            execPath.Value() + Separator() + ".." + Separator() + "pandastdlib" + Separator() + "arkstdlib.abc";
        options.SetBootPandaFiles({pandaStdLib});
        Runtime::Create(options);

        auto pf = panda_file::OpenPandaFileOrZip(GetPandaFilePath());
        if (pf != nullptr) {
            pfId_ = reinterpret_cast<uintptr_t>(pf.get());
        }
        Runtime::GetCurrent()->GetClassLinker()->AddPandaFile(std::move(pf));
        CollectMethodIds();

        thread_ = ark::MTManagedThread::GetCurrent();
    }

    void TearDown() override
    {
        Runtime::Destroy();
    }

    void CollectMethodIds()
    {
        if (pfId_ == 0) {
            return;
        }
        auto *pf = reinterpret_cast<const panda_file::File *>(pfId_);
        auto classes = pf->GetClasses();
        for (size_t i = 0; i < classes.Size(); ++i) {
            panda_file::File::EntityId classId(classes[i]);
            if (pf->IsExternal(classId)) {
                continue;
            }

            panda_file::ClassDataAccessor cda(*pf, classId);
            cda.EnumerateMethods(
                [this](panda_file::MethodDataAccessor &mda) { methodIds_.push_back(mda.GetMethodId().GetOffset()); });
        }
    }

    void FullfillFakeSample(SampleInfo *ps)
    {
        uint32_t methodId = HasTestMethodId() ? GetTestMethodId(0) : 0;
        for (uint32_t i = 0; i < SampleInfo::StackInfo::MAX_STACK_DEPTH; ++i) {
            ps->stackInfo.managedStack[i] = {methodId, pfId_};
        }
        ps->threadInfo.threadId = GetThreadId();
        ps->stackInfo.managedStackSize = SampleInfo::StackInfo::MAX_STACK_DEPTH;
    }

    // Friend wrappers for accesing samplers private fields
    static os::thread::NativeHandleType ExtractListenerTid(const Sampler *sPtr)
    {
        return sPtr->listenerTid_;
    }

    static os::thread::NativeHandleType ExtractSamplerTid(const Sampler *sPtr)
    {
        return sPtr->samplerTid_;
    }

    static PandaSet<os::thread::ThreadId> ExtractManagedThreads(Sampler *sPtr)
    {
        // Sending a copy to avoid of datarace
        os::memory::LockHolder holder(sPtr->managedThreadsLock_);
        PandaSet<os::thread::ThreadId> managedThreadsCopy = sPtr->managedThreads_;
        return managedThreadsCopy;
    }

    static size_t ExtractLoadedPFSize(Sampler *sPtr)
    {
        os::memory::LockHolder holder(sPtr->loadedPfsLock_);
        return sPtr->loadedPfs_.size();
    }

    static std::array<int, 2> ExtractPipes(const Sampler *sPtr)
    {
        // Sending a copy to avoid of datarace
        return sPtr->communicator_.listenerPipe_;
    }

    static bool ExtractIsActive(const Sampler *sPtr)
    {
        // Atomic with acquire order reason: To ensure start/stop load correctly
        return sPtr->isActive_.load(std::memory_order_acquire);
    }

    uint32_t GetThreadId()
    {
        return os::thread::GetCurrentThreadId();
    }

    uintptr_t GetTestPandaFilePtr() const
    {
        return pfId_;
    }

    uint32_t GetTestMethodId(size_t index = 0) const
    {
        if (index < methodIds_.size()) {
            return methodIds_[index];
        }
        return 0;
    }

    bool HasTestMethodId() const
    {
        return !methodIds_.empty();
    }

protected:
    ark::MTManagedThread *thread_ {nullptr};
    uintptr_t pfId_ {0};
    uint32_t checksum_ {0};
    std::vector<uint32_t> methodIds_;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

TEST_F(SamplerTest, SamplerInitTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);

    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename)), false);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);

    // Second run
    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

TEST_F(SamplerTest, InspectorSamplerInitTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);

    ASSERT_EQ(sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    ASSERT_EQ(sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD)), false);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);

    // Second run
    ASSERT_EQ(sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

void RunManagedThread(std::atomic<bool> *syncFlag)
{
    auto *mThr = ark::MTManagedThread::Create(ark::Runtime::GetCurrent(), ark::Runtime::GetCurrent()->GetPandaVM());
    mThr->ManagedCodeBegin();

    *syncFlag = true;
    while (*syncFlag) {
        // Calling safepoint 'cause starting profiler required to stop all managed threads
        ManagedThread::GetCurrent()->Safepoint();
    }

    mThr->ManagedCodeEnd();
    mThr->Destroy();
}

void RunManagedThreadAndSaveThreadId(std::atomic<bool> *syncFlag, os::thread::ThreadId *id)
{
    auto *mThr = ark::MTManagedThread::Create(ark::Runtime::GetCurrent(), ark::Runtime::GetCurrent()->GetPandaVM());
    mThr->ManagedCodeBegin();

    *id = os::thread::GetCurrentThreadId();
    *syncFlag = true;
    while (*syncFlag) {
        // Calling safepoint 'cause starting profiler required to stop all managed threads
        ManagedThread::GetCurrent()->Safepoint();
    }

    mThr->ManagedCodeEnd();
    mThr->Destroy();
}

void RunNativeThread(std::atomic<bool> *syncFlag)
{
    auto *mThr = ark::MTManagedThread::Create(ark::Runtime::GetCurrent(), ark::Runtime::GetCurrent()->GetPandaVM());

    *syncFlag = true;
    while (*syncFlag) {
    }

    mThr->Destroy();
}

// Testing notification thread started/finished
TEST_F(SamplerTest, SamplerEventThreadNotificationTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    ASSERT_FALSE(ExtractManagedThreads(sp).empty());
    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    std::atomic<bool> syncFlag1 = false;
    std::atomic<bool> syncFlag2 = false;
    std::atomic<bool> syncFlag3 = false;
    std::thread managedThread1(RunManagedThread, &syncFlag1);
    std::thread managedThread2(RunManagedThread, &syncFlag2);
    std::thread managedThread3(RunManagedThread, &syncFlag3);

    while (!syncFlag1 || !syncFlag2 || !syncFlag3) {
        ;
    }
    ASSERT_EQ(ExtractManagedThreads(sp).size(), 4UL);

    syncFlag1 = false;
    syncFlag2 = false;
    syncFlag3 = false;
    managedThread1.join();
    managedThread2.join();
    managedThread3.join();

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

TEST_F(SamplerTest, InspectorSamplerEventThreadNotificationTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    ASSERT_EQ(sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    ASSERT_FALSE(ExtractManagedThreads(sp).empty());
    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    std::atomic<bool> syncFlag1 = false;
    std::atomic<bool> syncFlag2 = false;
    std::atomic<bool> syncFlag3 = false;
    std::thread managedThread1(RunManagedThread, &syncFlag1);
    std::thread managedThread2(RunManagedThread, &syncFlag2);
    std::thread managedThread3(RunManagedThread, &syncFlag3);

    while (!syncFlag1 || !syncFlag2 || !syncFlag3) {
        ;
    }
    ASSERT_EQ(ExtractManagedThreads(sp).size(), 4UL);

    syncFlag1 = false;
    syncFlag2 = false;
    syncFlag3 = false;
    managedThread1.join();
    managedThread2.join();
    managedThread3.join();

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

// Testing notification thread started/finished
TEST_F(SamplerTest, SamplerCheckThreadIdTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    std::atomic<bool> syncFlag1 = false;
    os::thread::ThreadId mtId = 0;
    std::thread managedThread1(RunManagedThreadAndSaveThreadId, &syncFlag1, &mtId);

    while (!syncFlag1) {
        ;
    }
    // only one additional managed thread must be running
    ASSERT_EQ(ExtractManagedThreads(sp).size(), 2UL);
    bool isPassed = false;

    for (const auto &elem : ExtractManagedThreads(sp)) {
        if (elem == mtId) {
            isPassed = true;
            break;
        }
    }
    ASSERT_TRUE(isPassed);

    syncFlag1 = false;
    managedThread1.join();

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

TEST_F(SamplerTest, InspectorSamplerCheckThreadIdTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    ASSERT_EQ(sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    std::atomic<bool> syncFlag1 = false;
    os::thread::ThreadId mtId = 0;
    std::thread managedThread1(RunManagedThreadAndSaveThreadId, &syncFlag1, &mtId);

    while (!syncFlag1) {
        ;
    }
    // only one additional managed thread must be running
    ASSERT_EQ(ExtractManagedThreads(sp).size(), 2UL);
    bool isPassed = false;

    for (const auto &elem : ExtractManagedThreads(sp)) {
        if (elem == mtId) {
            isPassed = true;
            break;
        }
    }
    ASSERT_TRUE(isPassed);

    syncFlag1 = false;
    managedThread1.join();

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

// Testing thread collection
TEST_F(SamplerTest, SamplerCollectThreadTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    std::atomic<bool> syncFlag1 = false;
    std::atomic<bool> syncFlag2 = false;
    std::atomic<bool> syncFlag3 = false;
    std::thread managedThread1(RunManagedThread, &syncFlag1);
    std::thread managedThread2(RunManagedThread, &syncFlag2);
    std::thread managedThread3(RunManagedThread, &syncFlag3);

    while (!syncFlag1 || !syncFlag2 || !syncFlag3) {
        ;
    }

    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 4UL);

    syncFlag1 = false;
    syncFlag2 = false;
    syncFlag3 = false;
    managedThread1.join();
    managedThread2.join();
    managedThread3.join();

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

TEST_F(SamplerTest, InspectorSamplerCollectThreadTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    std::atomic<bool> syncFlag1 = false;
    std::atomic<bool> syncFlag2 = false;
    std::atomic<bool> syncFlag3 = false;
    std::thread managedThread1(RunManagedThread, &syncFlag1);
    std::thread managedThread2(RunManagedThread, &syncFlag2);
    std::thread managedThread3(RunManagedThread, &syncFlag3);

    while (!syncFlag1 || !syncFlag2 || !syncFlag3) {
        ;
    }

    ASSERT_EQ(sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 4UL);

    syncFlag1 = false;
    syncFlag2 = false;
    syncFlag3 = false;
    managedThread1.join();
    managedThread2.join();
    managedThread3.join();

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

// Testing native thread collection
TEST_F(SamplerTest, SamplerCollectNativeThreadTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    std::atomic<bool> syncFlag1 = false;
    std::atomic<bool> syncFlag2 = false;
    std::atomic<bool> syncFlag3 = false;
    std::thread managedThread1(RunManagedThread, &syncFlag1);
    std::thread nativeThread2(RunNativeThread, &syncFlag2);

    while (!syncFlag1 || !syncFlag2) {
        ;
    }

    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    // two additional threads must be running - a managed and a native one
    ASSERT_EQ(ExtractManagedThreads(sp).size(), 3UL);
    std::thread nativeThread3(RunNativeThread, &syncFlag3);
    while (!syncFlag3) {
        ;
    }

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 4UL);

    syncFlag1 = false;
    syncFlag2 = false;
    syncFlag3 = false;
    managedThread1.join();
    nativeThread2.join();
    nativeThread3.join();

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

TEST_F(SamplerTest, InspectorSamplerCollectNativeThreadTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    std::atomic<bool> syncFlag1 = false;
    std::atomic<bool> syncFlag2 = false;
    std::atomic<bool> syncFlag3 = false;
    std::thread managedThread1(RunManagedThread, &syncFlag1);
    std::thread nativeThread2(RunNativeThread, &syncFlag2);

    while (!syncFlag1 || !syncFlag2) {
        ;
    }

    ASSERT_EQ(sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD)), true);
    ASSERT_NE(ExtractListenerTid(sp), 0);
    ASSERT_NE(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), true);

    // two additional threads must be running - a managed and a native one
    ASSERT_EQ(ExtractManagedThreads(sp).size(), 3UL);
    std::thread nativeThread3(RunNativeThread, &syncFlag3);
    while (!syncFlag3) {
        ;
    }

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 4UL);

    syncFlag1 = false;
    syncFlag2 = false;
    syncFlag3 = false;
    managedThread1.join();
    nativeThread2.join();
    nativeThread3.join();

    ASSERT_EQ(ExtractManagedThreads(sp).size(), 1);

    sp->Stop();
    ASSERT_EQ(ExtractListenerTid(sp), 0);
    ASSERT_EQ(ExtractSamplerTid(sp), 0);
    ASSERT_EQ(ExtractIsActive(sp), false);
    Sampler::Destroy(sp);
}

// Testing pipes
TEST_F(SamplerTest, SamplerPipesTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);
    sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename));

    ASSERT_NE(ExtractPipes(sp)[ThreadCommunicator::PIPE_READ_ID], 0);
    ASSERT_NE(ExtractPipes(sp)[ThreadCommunicator::PIPE_WRITE_ID], 0);

    sp->Stop();
    Sampler::Destroy(sp);
}

TEST_F(SamplerTest, InspectorSamplerPipesTest)
{
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);
    sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD));

    ASSERT_NE(ExtractPipes(sp)[ThreadCommunicator::PIPE_READ_ID], 0);
    ASSERT_NE(ExtractPipes(sp)[ThreadCommunicator::PIPE_WRITE_ID], 0);

    sp->Stop();
    Sampler::Destroy(sp);
}

// Stress testing restart
TEST_F(SamplerTest, ProfilerRestartStressTest)
{
    constexpr size_t CURRENT_TEST_THRESHOLD = TEST_CYCLE_THRESHOLD / 10;
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    for (uint32_t i = 0; i < CURRENT_TEST_THRESHOLD; i++) {
        ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(g_profilerFilename)), true);
        sp->Stop();
    }

    Sampler::Destroy(sp);
}

TEST_F(SamplerTest, InspectorProfilerRestartStressTest)
{
    constexpr size_t CURRENT_TEST_THRESHOLD = TEST_CYCLE_THRESHOLD / 10;
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);

    for (uint32_t i = 0; i < CURRENT_TEST_THRESHOLD; i++) {
        ASSERT_EQ(sp->Start(std::make_unique<InspectorStreamWriter>(G_SAMPLES_RECORD)), true);
        sp->Stop();
    }

    Sampler::Destroy(sp);
}

TEST_F(SamplerTest, ThreadCommunicatorTest)
{
    ThreadCommunicator communicator;

    SampleInfo sampleInput;
    SampleInfo sampleOutput;
    FullfillFakeSample(&sampleInput);
    ASSERT_TRUE(communicator.Init());
    ASSERT_TRUE(communicator.SendSample(sampleInput));
    ASSERT_TRUE(communicator.ReadSample(&sampleOutput));
    ASSERT_EQ(sampleOutput, sampleInput);
}

static void CommunicatorStressWritterThread(const ThreadCommunicator *com, const SampleInfo &sample,
                                            uint32_t messagesAmount)
{
    for (uint32_t i = 0; i < messagesAmount; ++i) {
        // If the sample write failed we retrying to send it
        if (!com->SendSample(sample)) {
            std::cerr << "Failed to send a sample" << std::endl;
            Runtime::Abort();
        }
    }
}

static constexpr uint32_t MESSAGES_AMOUNT = TEST_CYCLE_THRESHOLD * 100;

TEST_F(SamplerTest, ThreadCommunicatorMultithreadTest)
{
    ThreadCommunicator communicator;
    SampleInfo sampleOutput;
    SampleInfo sampleInput;
    FullfillFakeSample(&sampleInput);
    ASSERT_TRUE(communicator.Init());

    std::thread sender(CommunicatorStressWritterThread, &communicator, sampleInput, MESSAGES_AMOUNT);
    for (uint32_t i = 0; i < MESSAGES_AMOUNT; ++i) {
        // If the sample write failed we retrying to send it
        if (!communicator.ReadSample(&sampleOutput)) {
            std::cerr << "Failed to read a sample" << std::endl;
            Runtime::Abort();
        }
        ASSERT_EQ(sampleOutput, sampleInput);
    }
    sender.join();
}

// Testing reader and writer by writing and reading from .aspt one sample
TEST_F(SamplerTest, StreamWriterReaderTest)
{
    const char *streamTestFilename = "stream_writer_reader_test.aspt";
    SampleInfo sampleOutput;
    SampleInfo sampleInput;

    {
        FileStreamWriter writer(streamTestFilename);
        FullfillFakeSample(&sampleInput);

        writer.WriteSample(sampleInput);
    }

    SampleReader reader(streamTestFilename);
    ASSERT_TRUE(reader.GetNextSample(&sampleOutput));
    ASSERT_EQ(sampleOutput, sampleInput);
    ASSERT_FALSE(reader.GetNextSample(&sampleOutput));
    ASSERT_FALSE(reader.GetNextModule(nullptr));
}

TEST_F(SamplerTest, InspectorStreamWriterReaderTest)
{
    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    SampleInfo sampleInput;

    {
        InspectorStreamWriter writer(samplesRecord);
        FullfillFakeSample(&sampleInput);

        writer.WriteSample(sampleInput);
    }

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
    ASSERT_NE((*allThreadsProfileInfos)[0]->nodeCount, 0);
    ASSERT_EQ((*allThreadsProfileInfos)[0]->startTime, 0);
    ASSERT_EQ((*allThreadsProfileInfos)[0]->stopTime, 0);
    ASSERT_EQ((*allThreadsProfileInfos)[0]->tid, GetThreadId());
}

// Testing reader and writer by writing and reading from .aspt lots of samples
TEST_F(SamplerTest, StreamWriterReaderLotsSamplesTest)
{
    constexpr size_t CURRENT_TEST_THRESHOLD = TEST_CYCLE_THRESHOLD * 100;
    const char *streamTestFilename = "stream_writer_reader_test_lots_samples.aspt";
    SampleInfo sampleOutput;
    SampleInfo sampleInput;

    {
        FileStreamWriter writer(streamTestFilename);
        FullfillFakeSample(&sampleInput);

        for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
            writer.WriteSample(sampleInput);
        }
    }

    SampleReader reader(streamTestFilename);
    for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
        ASSERT_TRUE(reader.GetNextSample(&sampleOutput));
        ASSERT_EQ(sampleOutput, sampleInput);
    }
    ASSERT_FALSE(reader.GetNextSample(&sampleOutput));
    ASSERT_FALSE(reader.GetNextModule(nullptr));
}

TEST_F(SamplerTest, InspectorStreamWriterReaderLotsSamplesTest)
{
    constexpr size_t CURRENT_TEST_THRESHOLD = TEST_CYCLE_THRESHOLD * 100;
    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    SampleInfo sampleInput;

    {
        InspectorStreamWriter writer(samplesRecord);
        FullfillFakeSample(&sampleInput);

        for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
            writer.WriteSample(sampleInput);
        }
    }

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
    ASSERT_NE((*allThreadsProfileInfos)[0]->nodeCount, 0);
    ASSERT_EQ((*allThreadsProfileInfos)[0]->startTime, 0);
    ASSERT_EQ((*allThreadsProfileInfos)[0]->stopTime, 0);
    ASSERT_EQ((*allThreadsProfileInfos)[0]->tid, GetThreadId());
    ASSERT_EQ((*allThreadsProfileInfos)[0]->timeDeltas.size(), CURRENT_TEST_THRESHOLD);
}

// Testing reader and writer by writing and reading from .aspt one module
TEST_F(SamplerTest, ModuleWriterReaderTest)
{
    const char *streamTestFilename = "stream_module_test_filename.aspt";
    FileInfo moduleInput = {pfId_, checksum_, "~/folder/folder/lib/panda_file.pa"};
    FileInfo moduleOutput = {};

    {
        FileStreamWriter writer(streamTestFilename);
        writer.WriteModule(moduleInput);
    }

    SampleReader reader(streamTestFilename);
    ASSERT_TRUE(reader.GetNextModule(&moduleOutput));
    ASSERT_EQ(moduleOutput, moduleInput);
    ASSERT_FALSE(reader.GetNextModule(&moduleOutput));
    ASSERT_FALSE(reader.GetNextSample(nullptr));
}

// Testing reader and writer by writing and reading from .aspt lots of modules
TEST_F(SamplerTest, ModuleWriterReaderLotsModulesTest)
{
    constexpr size_t CURRENT_TEST_THRESHOLD = TEST_CYCLE_THRESHOLD * 100;
    const char *streamTestFilename = "stream_lots_modules_test_filename.aspt";
    FileInfo moduleInput = {pfId_, checksum_, "~/folder/folder/lib/panda_file.pa"};
    FileInfo moduleOutput = {};

    {
        FileStreamWriter writer(streamTestFilename);
        for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
            writer.WriteModule(moduleInput);
        }
    }

    SampleReader reader(streamTestFilename);
    for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
        ASSERT_TRUE(reader.GetNextModule(&moduleOutput));
        ASSERT_EQ(moduleOutput, moduleInput);
    }
    ASSERT_FALSE(reader.GetNextModule(&moduleOutput));
    ASSERT_FALSE(reader.GetNextSample(nullptr));
}

// Testing reader and writer by writing and reading from .aspt lots of modules
TEST_F(SamplerTest, WriterReaderLotsRowsModulesAndSamplesTest)
{
    constexpr size_t CURRENT_TEST_THRESHOLD = TEST_CYCLE_THRESHOLD * 100;
    const char *streamTestFilename = "stream_lots_modules_and_samples_test_filename.aspt";
    FileInfo moduleInput = {pfId_, checksum_, "~/folder/folder/lib/panda_file.pa"};
    FileInfo moduleOutput = {};
    SampleInfo sampleOutput;
    SampleInfo sampleInput;

    {
        FileStreamWriter writer(streamTestFilename);
        FullfillFakeSample(&sampleInput);
        for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
            writer.WriteModule(moduleInput);
            writer.WriteSample(sampleInput);
        }
    }

    SampleReader reader(streamTestFilename);
    for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
        ASSERT_TRUE(reader.GetNextModule(&moduleOutput));
        ASSERT_EQ(moduleOutput, moduleInput);
    }

    for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
        ASSERT_TRUE(reader.GetNextSample(&sampleOutput));
        ASSERT_EQ(sampleOutput, sampleInput);
    }

    ASSERT_FALSE(reader.GetNextModule(&moduleOutput));
    ASSERT_FALSE(reader.GetNextSample(&sampleOutput));
}

// Send sample to listener and check it inside the file
TEST_F(SamplerTest, ListenerWriteFakeSampleTest)
{
    const char *streamTestFilename = "listener_write_fake_sample_test.aspt";
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);
    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(streamTestFilename)), true);

    SampleInfo sampleOutput;
    SampleInfo sampleInput;
    FullfillFakeSample(&sampleInput);
    sp->GetCommunicator().SendSample(sampleInput);
    sp->Stop();

    bool status = true;
    bool isPassed = false;
    SampleReader reader(streamTestFilename);
    while (status) {
        status = reader.GetNextSample(&sampleOutput);
        if (sampleOutput == sampleInput) {
            isPassed = true;
            break;
        }
    }

    ASSERT_TRUE(isPassed);

    Sampler::Destroy(sp);
}

// Send lots of sample to listener and check it inside the file
TEST_F(SamplerTest, ListenerWriteLotsFakeSampleTest)
{
    constexpr size_t CURRENT_TEST_THRESHOLD = TEST_CYCLE_THRESHOLD * 100;
    const char *streamTestFilename = "listener_write_lots_fake_sample_test.aspt";
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);
    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(streamTestFilename)), true);

    SampleInfo sampleOutput;
    SampleInfo sampleInput;
    size_t sentSamplesCounter = 0;
    FullfillFakeSample(&sampleInput);
    for (size_t i = 0; i < CURRENT_TEST_THRESHOLD; ++i) {
        if (sp->GetCommunicator().SendSample(sampleInput)) {
            ++sentSamplesCounter;
        }
    }
    sp->Stop();

    bool status = true;
    size_t amountOfSamples = 0;
    SampleReader reader(streamTestFilename);
    while (status) {
        if (sampleOutput == sampleInput) {
            ++amountOfSamples;
        }
        status = reader.GetNextSample(&sampleOutput);
    }

    ASSERT_EQ(amountOfSamples, sentSamplesCounter);

    Sampler::Destroy(sp);
}

// Checking that sampler collect panda files correctly
TEST_F(SamplerTest, CollectPandaFilesTest)
{
    const char *streamTestFilename = "collect_panda_file_test.aspt";
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);
    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(streamTestFilename)), true);
    sp->Stop();

    FileInfo moduleInfo;
    SampleReader reader(streamTestFilename);
    bool status = false;
    while (reader.GetNextModule(&moduleInfo)) {
        auto pfPtr = reinterpret_cast<panda_file::File *>(moduleInfo.ptr);
        ASSERT_EQ(pfPtr->GetFullFileName(), moduleInfo.pathname);
        status = true;
    }
    ASSERT_TRUE(status);
    Sampler::Destroy(sp);
}

// Checking that sampler collect panda files correctly
TEST_F(SamplerTest, WriteModuleEventTest)
{
    const char *streamTestFilename = "collect_panda_file_test.aspt";
    auto *sp = Sampler::Create();
    ASSERT_NE(sp, nullptr);
    ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(streamTestFilename)), true);

    auto execPath = ark::os::file::File::GetExecutablePath();
    std::string pandafile =
        execPath.Value() + Separator() + ".." + Separator() + "pandastdlib" + Separator() + "arkstdlib.abc";

    auto pf = panda_file::OpenPandaFileOrZip(pandafile);
    Runtime::GetCurrent()->GetClassLinker()->AddPandaFile(std::move(pf));
    // NOTE:
    // It's necessary to add assert that only one module is loaded
    // But simply add assert on loaded size is UB, because such assert may fail if the WriteLoadedPandaFiles
    // managed to dump the loaded module into the trace before the assert call occurs in this test

    sp->Stop();

    FileInfo moduleInfo;
    SampleReader reader(streamTestFilename);
    bool status = false;
    while (reader.GetNextModule(&moduleInfo)) {
        auto pfPtr = reinterpret_cast<panda_file::File *>(moduleInfo.ptr);
        ASSERT_EQ(pfPtr->GetFullFileName(), moduleInfo.pathname);
        status = true;
    }
    ASSERT_TRUE(status);

    Sampler::Destroy(sp);
}

// Sampling big pandasm program and convert it
TEST_F(SamplerTest, ProfilerSamplerSignalHandlerTest)
{
    const char *streamTestFilename = "sampler_signal_handler_test.aspt";
    const char *resultTestFilename = "sampler_signal_handler_test.csv";
    size_t sampleCounter = 0;

    {
        auto *sp = Sampler::Create();
        ASSERT_NE(sp, nullptr);
        ASSERT_EQ(sp->Start(std::make_unique<FileStreamWriter>(streamTestFilename)), true);

        {
            ASSERT_TRUE(Runtime::GetCurrent()->Execute("_GLOBAL::main", {}));
        }
        sp->Stop();

        SampleInfo sample;
        SampleReader reader(streamTestFilename);
        bool isFind = false;

        while (reader.GetNextSample(&sample)) {
            ++sampleCounter;
            if (sample.stackInfo.managedStackSize == 2U) {
                isFind = true;
                continue;
            }
            ASSERT_NE(sample.stackInfo.managedStackSize, 0);
            ASSERT_EQ(GetThreadId(), sample.threadInfo.threadId);
        }

        ASSERT_EQ(isFind, true);

        Sampler::Destroy(sp);
    }

    // Checking converter
    {
        AsptConverter conv(streamTestFilename);
        ASSERT_EQ(conv.CollectTracesStats(), sampleCounter);
        ASSERT_TRUE(conv.CollectModules());
        ASSERT_TRUE(conv.DumpResolvedTracesAsCSV(resultTestFilename));
    }
}

// Testing hybrid stack frame identification
TEST_F(SamplerTest, HybridStackFrameIdentificationTest)
{
    SampleInfo::ManagedStackFrameId staticFrame;
    staticFrame.pandaFilePtr = 0x1000;  // Actual panda file pointer
    staticFrame.fileId = FILE_ID_100;
    staticFrame.bcOffset = BC_OFFSET_10;
    staticFrame.isStaticFrame = true;
    staticFrame.extFrameData = nullptr;

    SampleInfo::ManagedStackFrameId dynamicFrame;
    dynamicFrame.pandaFilePtr = 0x2000;
    dynamicFrame.fileId = FILE_ID_100;
    dynamicFrame.bcOffset = 0;
    dynamicFrame.isStaticFrame = false;
    dynamicFrame.extFrameData = reinterpret_cast<void *>(0x12345678);

    SampleInfo::ManagedStackFrameId bridgeFrame;
    bridgeFrame.pandaFilePtr = helpers::ToUnderlying(FrameKind::BRIDGE);
    bridgeFrame.fileId = 0;
    bridgeFrame.bcOffset = 0;
    bridgeFrame.isStaticFrame = true;
    bridgeFrame.extFrameData = nullptr;

    // Verify frame type identification
    bool isStatic = (staticFrame.isStaticFrame && staticFrame.pandaFilePtr != helpers::ToUnderlying(FrameKind::BRIDGE));
    bool isDynamic = (!dynamicFrame.isStaticFrame);
    bool isBridge = (bridgeFrame.pandaFilePtr == helpers::ToUnderlying(FrameKind::BRIDGE));

    ASSERT_TRUE(isStatic);
    ASSERT_TRUE(isDynamic);
    ASSERT_TRUE(isBridge);
    ASSERT_TRUE(bridgeFrame < staticFrame);
    ASSERT_TRUE(staticFrame < dynamicFrame);
}

// Testing ManagedStackFrameId equality operator for static frames
TEST_F(SamplerTest, ManagedStackFrameIdStaticEqualityTest)
{
    SampleInfo::ManagedStackFrameId frame1;
    frame1.pandaFilePtr = 0x1000;
    frame1.fileId = FILE_ID_100;
    frame1.bcOffset = BC_OFFSET_10;

    SampleInfo::ManagedStackFrameId frame2;
    frame2.pandaFilePtr = 0x1000;
    frame2.fileId = FILE_ID_100;
    frame2.bcOffset = BC_OFFSET_10;

    SampleInfo::ManagedStackFrameId frame3;
    frame3.pandaFilePtr = 0x1000;
    frame3.fileId = FILE_ID_200;
    frame3.bcOffset = BC_OFFSET_10;

    ASSERT_EQ(frame1, frame2);
    ASSERT_NE(frame1, frame3);
}

// Testing SampleInfo hash function with dynamic frames
TEST_F(SamplerTest, SampleInfoHashTest)
{
    SampleInfo sample1;
    sample1.threadInfo.threadId = 1;
    sample1.stackInfo.managedStackSize = 1;
    sample1.stackInfo.managedStack[0].pandaFilePtr = 0x1000;
    sample1.stackInfo.managedStack[0].isStaticFrame = true;
    sample1.stackInfo.managedStack[0].extFrameData = nullptr;

    SampleInfo sample2;
    sample2.threadInfo.threadId = 1;
    sample2.stackInfo.managedStackSize = 1;
    sample2.stackInfo.managedStack[0].pandaFilePtr = 0x1000;
    sample2.stackInfo.managedStack[0].isStaticFrame = true;
    sample2.stackInfo.managedStack[0].extFrameData = nullptr;

    std::hash<SampleInfo> hashFunc;
    size_t hash1 = hashFunc(sample1);
    size_t hash2 = hashFunc(sample2);

    // Same dynamic frame (both nullptr) should produce same hash
    ASSERT_EQ(hash1, hash2);
}

// Testing hybrid stack sample with mixed frames
TEST_F(SamplerTest, HybridStackMixedFramesTest)
{
    uintptr_t testPandaFilePtr = GetTestPandaFilePtr();
    if (testPandaFilePtr == 0U) {
        GTEST_SKIP() << "Test panda file not available, skipping test";
    }
    ASSERT_TRUE(HasTestMethodId()) << "Failed to get test method ids";

    uint32_t testMethodId = GetTestMethodId(0);

    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    SampleInfo sample;
    sample.threadInfo.threadId = GetThreadId();
    sample.stackInfo.managedStackSize = NUMBER_3;

    // Dynamic frame (nullptr is safe, will use default values)
    sample.stackInfo.managedStack[0].pandaFilePtr = 0x1000;
    sample.stackInfo.managedStack[0].fileId = 0;
    sample.stackInfo.managedStack[0].bcOffset = 0;
    sample.stackInfo.managedStack[0].isStaticFrame = false;
    sample.stackInfo.managedStack[0].extFrameData = nullptr;

    // Bridge frame (should be skipped in processing)
    sample.stackInfo.managedStack[1].pandaFilePtr = helpers::ToUnderlying(FrameKind::BRIDGE);
    sample.stackInfo.managedStack[1].fileId = 0;
    sample.stackInfo.managedStack[1].bcOffset = 0;
    sample.stackInfo.managedStack[1].isStaticFrame = true;
    sample.stackInfo.managedStack[1].extFrameData = nullptr;

    // Static frame (using real panda file pointer)
    sample.stackInfo.managedStack[NUMBER_2].pandaFilePtr = testPandaFilePtr;
    sample.stackInfo.managedStack[NUMBER_2].fileId = testMethodId;
    sample.stackInfo.managedStack[NUMBER_2].bcOffset = BC_OFFSET_10;
    sample.stackInfo.managedStack[NUMBER_2].isStaticFrame = true;
    sample.stackInfo.managedStack[NUMBER_2].extFrameData = nullptr;

    InspectorStreamWriter writer(samplesRecord);
    writer.WriteSample(sample);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
    ASSERT_NE((*allThreadsProfileInfos)[0]->nodeCount, 0);
}

// Testing max stack depth with hybrid frames
TEST_F(SamplerTest, HybridStackMaxDepthTest)
{
    uintptr_t testPandaFilePtr = GetTestPandaFilePtr();
    if (testPandaFilePtr == 0U) {
        GTEST_SKIP() << "Failed to get test panda file pointer";
    }
    ASSERT_TRUE(HasTestMethodId()) << "Failed to get test method ids";

    uint32_t testMethodId = GetTestMethodId(0);

    SampleInfo sample;
    sample.threadInfo.threadId = GetThreadId();
    sample.stackInfo.managedStackSize = SampleInfo::StackInfo::MAX_STACK_DEPTH;

    for (size_t i = 0; i < SampleInfo::StackInfo::MAX_STACK_DEPTH; ++i) {
        if (i % NUMBER_2 == 0) {
            // Dynamic frame (nullptr is safe)
            sample.stackInfo.managedStack[i].pandaFilePtr = 0x1000;
            sample.stackInfo.managedStack[i].isStaticFrame = false;
            sample.stackInfo.managedStack[i].extFrameData = nullptr;
        } else {
            // Static frame (using real panda file pointer)
            sample.stackInfo.managedStack[i].pandaFilePtr = testPandaFilePtr;
            sample.stackInfo.managedStack[i].fileId = testMethodId;
            sample.stackInfo.managedStack[i].bcOffset = static_cast<uint32_t>(i * NUMBER_10);
        }
    }

    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    InspectorStreamWriter writer(samplesRecord);
    writer.WriteSample(sample);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
}

// Testing empty hybrid stack
TEST_F(SamplerTest, HybridStackEmptyTest)
{
    SampleInfo sample;
    sample.threadInfo.threadId = GetThreadId();
    sample.stackInfo.managedStackSize = 0;

    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    InspectorStreamWriter writer(samplesRecord);
    writer.WriteSample(sample);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
}

// Testing single dynamic frame
TEST_F(SamplerTest, HybridStackSingleDynamicFrameTest)
{
    SampleInfo sample;
    sample.threadInfo.threadId = GetThreadId();
    sample.stackInfo.managedStackSize = 1;
    sample.stackInfo.managedStack[0].pandaFilePtr = 0x1000;
    sample.stackInfo.managedStack[0].fileId = 0;
    sample.stackInfo.managedStack[0].bcOffset = 0;
    sample.stackInfo.managedStack[0].isStaticFrame = false;
    sample.stackInfo.managedStack[0].extFrameData = nullptr;

    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    InspectorStreamWriter writer(samplesRecord);
    writer.WriteSample(sample);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
    // 3 default nodes (root, program, idle), dynamic frame skipped due to null extFrameData
    ASSERT_EQ((*allThreadsProfileInfos)[0]->nodeCount, NUMBER_3);
}

// Testing single static frame
TEST_F(SamplerTest, HybridStackSingleStaticFrameTest)
{
    uintptr_t testPandaFilePtr = GetTestPandaFilePtr();
    if (testPandaFilePtr == 0U) {
        GTEST_SKIP() << "Test panda file not available, skipping test";
    }
    ASSERT_TRUE(HasTestMethodId()) << "Failed to get test method ids";

    uint32_t testMethodId = GetTestMethodId(0);

    SampleInfo sample;
    sample.threadInfo.threadId = GetThreadId();
    sample.stackInfo.managedStackSize = 1;
    sample.stackInfo.managedStack[0].pandaFilePtr = testPandaFilePtr;
    sample.stackInfo.managedStack[0].fileId = testMethodId;
    sample.stackInfo.managedStack[0].bcOffset = BC_OFFSET_10;
    sample.stackInfo.managedStack[0].isStaticFrame = true;
    sample.stackInfo.managedStack[0].extFrameData = nullptr;

    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    InspectorStreamWriter writer(samplesRecord);
    writer.WriteSample(sample);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
    // 3 default nodes (root, program, idle) + 1 static frame
    ASSERT_EQ((*allThreadsProfileInfos)[0]->nodeCount, NUMBER_4);
}

// Testing all dynamic frames stack
TEST_F(SamplerTest, HybridStackAllDynamicFramesTest)
{
    constexpr size_t STACK_SIZE = 5;
    SampleInfo sample;
    sample.threadInfo.threadId = GetThreadId();
    sample.stackInfo.managedStackSize = STACK_SIZE;

    for (size_t i = 0; i < STACK_SIZE; ++i) {
        sample.stackInfo.managedStack[i].pandaFilePtr = 0x1000;
        sample.stackInfo.managedStack[i].fileId = 0;
        sample.stackInfo.managedStack[i].bcOffset = 0;
        sample.stackInfo.managedStack[i].isStaticFrame = false;
        sample.stackInfo.managedStack[i].extFrameData = nullptr;
    }

    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    InspectorStreamWriter writer(samplesRecord);
    writer.WriteSample(sample);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
    // 3 default nodes (root, program, idle), dynamic frames skipped due to null extFrameData
    ASSERT_EQ((*allThreadsProfileInfos)[0]->nodeCount, NUMBER_3);
}

// Testing all static frames stack
TEST_F(SamplerTest, HybridStackAllStaticFramesTest)
{
    uintptr_t testPandaFilePtr = GetTestPandaFilePtr();
    if (testPandaFilePtr == 0U) {
        GTEST_SKIP() << "Test panda file not available, skipping test";
    }
    ASSERT_TRUE(HasTestMethodId()) << "Failed to get test method ids";

    constexpr size_t STACK_SIZE = NUMBER_5;
    SampleInfo sample;
    sample.threadInfo.threadId = GetThreadId();
    sample.stackInfo.managedStackSize = STACK_SIZE;

    uint32_t testMethodId = GetTestMethodId(0);
    for (size_t i = 0; i < STACK_SIZE; ++i) {
        sample.stackInfo.managedStack[i].pandaFilePtr = testPandaFilePtr;
        sample.stackInfo.managedStack[i].fileId = testMethodId;
        sample.stackInfo.managedStack[i].bcOffset = static_cast<uint32_t>(i * NUMBER_10);
        sample.stackInfo.managedStack[i].isStaticFrame = true;
        sample.stackInfo.managedStack[i].extFrameData = nullptr;
    }

    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    InspectorStreamWriter writer(samplesRecord);
    writer.WriteSample(sample);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
}

// Testing bridge frame filtering
TEST_F(SamplerTest, HybridStackBridgeFrameFilteringTest)
{
    uintptr_t testPandaFilePtr = GetTestPandaFilePtr();
    if (testPandaFilePtr == 0U) {
        GTEST_SKIP() << "Test panda file not available, skipping test";
    }
    ASSERT_TRUE(HasTestMethodId()) << "Failed to get test method ids";

    uint32_t testMethodId = GetTestMethodId(0);

    SampleInfo sample;
    sample.threadInfo.threadId = GetThreadId();
    sample.stackInfo.managedStackSize = NUMBER_4;

    // Dynamic frame (extFrameData is null, will be skipped in processing)
    sample.stackInfo.managedStack[0].pandaFilePtr = 0x1000;
    sample.stackInfo.managedStack[0].isStaticFrame = false;
    sample.stackInfo.managedStack[0].extFrameData = nullptr;

    // Bridge frame (should be skipped)
    sample.stackInfo.managedStack[1].pandaFilePtr = helpers::ToUnderlying(FrameKind::BRIDGE);
    sample.stackInfo.managedStack[1].isStaticFrame = true;
    sample.stackInfo.managedStack[1].extFrameData = nullptr;

    // Another bridge frame (should be skipped)
    sample.stackInfo.managedStack[NUMBER_2].pandaFilePtr = helpers::ToUnderlying(FrameKind::BRIDGE);
    sample.stackInfo.managedStack[NUMBER_2].isStaticFrame = true;
    sample.stackInfo.managedStack[NUMBER_2].extFrameData = nullptr;

    // Static frame (using real panda file pointer)
    sample.stackInfo.managedStack[NUMBER_3].pandaFilePtr = testPandaFilePtr;
    sample.stackInfo.managedStack[NUMBER_3].fileId = testMethodId;
    sample.stackInfo.managedStack[NUMBER_3].bcOffset = BC_OFFSET_10;

    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();
    InspectorStreamWriter writer(samplesRecord);
    writer.WriteSample(sample);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
    // 3 default nodes (root, program, idle) + 1 static frame (dynamic + bridges skipped)
    ASSERT_EQ((*allThreadsProfileInfos)[0]->nodeCount, NUMBER_4);
}

// Testing multiple hybrid stack samples
TEST_F(SamplerTest, MultipleHybridStackSamplesTest)
{
    uintptr_t testPandaFilePtr = GetTestPandaFilePtr();
    if (testPandaFilePtr == 0U) {
        GTEST_SKIP() << "Test panda file not available, skipping test";
    }
    ASSERT_TRUE(HasTestMethodId()) << "Failed to get test method ids";

    constexpr size_t SAMPLES_COUNT = NUMBER_10;
    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();

    uint32_t testMethodId = GetTestMethodId(0);
    for (size_t i = 0; i < SAMPLES_COUNT; ++i) {
        SampleInfo sample;
        sample.threadInfo.threadId = GetThreadId();
        sample.stackInfo.managedStackSize = NUMBER_2;

        sample.stackInfo.managedStack[0].pandaFilePtr = 0x1000;
        sample.stackInfo.managedStack[0].isStaticFrame = false;
        sample.stackInfo.managedStack[0].extFrameData = nullptr;

        sample.stackInfo.managedStack[1].pandaFilePtr = testPandaFilePtr;
        sample.stackInfo.managedStack[1].fileId = testMethodId;
        sample.stackInfo.managedStack[1].bcOffset = static_cast<uint32_t>(i * NUMBER_10);

        InspectorStreamWriter writer(samplesRecord);
        writer.WriteSample(sample);
    }

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), 1);
    ASSERT_EQ((*allThreadsProfileInfos)[0]->timeDeltas.size(), SAMPLES_COUNT);
}

// Testing hybrid stack with different thread IDs
TEST_F(SamplerTest, HybridStackDifferentThreadIdsTest)
{
    const std::shared_ptr<SamplesRecord> samplesRecord = std::make_shared<SamplesRecord>();

    SampleInfo sample1;
    sample1.threadInfo.threadId = 1;
    sample1.stackInfo.managedStackSize = 1;
    sample1.stackInfo.managedStack[0].pandaFilePtr = 0x1000;
    sample1.stackInfo.managedStack[0].isStaticFrame = false;
    sample1.stackInfo.managedStack[0].extFrameData = nullptr;

    SampleInfo sample2;
    sample2.threadInfo.threadId = NUMBER_2;
    sample2.stackInfo.managedStackSize = 1;
    sample2.stackInfo.managedStack[0].pandaFilePtr = 0x1000;
    sample2.stackInfo.managedStack[0].isStaticFrame = false;
    sample2.stackInfo.managedStack[0].extFrameData = nullptr;

    InspectorStreamWriter writer1(samplesRecord);
    writer1.WriteSample(sample1);

    InspectorStreamWriter writer2(samplesRecord);
    writer2.WriteSample(sample2);

    samplesRecord->SetMainThreadIds(1, 1);

    auto allThreadsProfileInfos = samplesRecord->GetAllThreadsProfileInfos();
    ASSERT_NE(allThreadsProfileInfos, nullptr);
    ASSERT_EQ(allThreadsProfileInfos->size(), NUMBER_2);
}
}  // namespace ark::tooling::sampler::test
