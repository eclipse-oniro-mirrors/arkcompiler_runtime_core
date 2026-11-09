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

#include <gtest/gtest.h>

#include "libarkbase/utils/asan_interface.h"
#include "libarkbase/mem/mem.h"
#include "libarkbase/os/mem.h"
#include "runtime/mem/tlab.h"

#include "mem/heap_manager.h"
#include "runtime/include/runtime.h"
#include "runtime/include/runtime_options.h"
#include "runtime/include/panda_vm.h"
#include "runtime/include/thread_scopes.h"
#include "runtime/include/mem/tlab_keeper.h"
#include "runtime/include/coretypes/string.h"
#include "test_utils.h"

namespace ark::mem {

constexpr size_t TLAB_TEST_SIZE = 4_MB;

class TLABTest : public testing::Test {
public:
    TLABTest()
    {
#ifdef PANDA_NIGHTLY_TEST_ON
        seed_ = time(NULL);
#else
        // NOLINTNEXTLINE(readability-magic-numbers)
        seed_ = 0x0BADDEAD;
#endif
        srand(seed_);
    }

    ~TLABTest() override
    {
        for (auto i : allocatedMemMmap_) {
            ark::os::mem::UnmapRaw(std::get<0>(i), std::get<1>(i));
        }
    }

    NO_COPY_SEMANTIC(TLABTest);
    NO_MOVE_SEMANTIC(TLABTest);

protected:
    TLAB *CreateNewTLAB()
    {
        void *mem = ark::os::mem::MapRWAnonymousInFirst4GB(ToVoidPtr(PANDA_32BITS_HEAP_START_ADDRESS), TLAB_TEST_SIZE);
        ASAN_UNPOISON_MEMORY_REGION(mem, TLAB_TEST_SIZE);
        std::pair<void *, size_t> newPair {mem, TLAB_TEST_SIZE};
        allocatedMemMmap_.push_back(newPair);
        auto tlabBuffOffs = AlignUp(ToUintPtr(mem) + sizeof(mem::TLAB), DEFAULT_ALIGNMENT_IN_BYTES) - ToUintPtr(mem);
        auto newTlab = new (mem) TLAB(ToVoidPtr(ToUintPtr(mem) + tlabBuffOffs), TLAB_TEST_SIZE - tlabBuffOffs);
        return newTlab;
    }

    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
    unsigned seed_ {};

private:
    std::vector<std::pair<void *, size_t>> allocatedMemMmap_;
};

TEST_F(TLABTest, AccessTest)
{
    static constexpr size_t ALLOC_SIZE = 512;
    static constexpr size_t ALLOC_COUNT = 500000;
    TLAB *tlab = CreateNewTLAB();
    ASSERT_TRUE(tlab != nullptr);
    // All accesses has been created according implementation as we want to create in JIT.
    bool overflow = false;
    auto *freePointerAddr =
        static_cast<ObjectPointerType *>(ToVoidPtr(ToUintPtr(tlab) + TLAB::TLABFreePointerOffset()));
    auto *endAddr = static_cast<ObjectPointerType *>(ToVoidPtr(ToUintPtr(tlab) + TLAB::TLABEndAddrOffset()));
    for (size_t i = 1; i < ALLOC_COUNT; i++) {
        ObjectPointerType oldFreePointer = (*freePointerAddr);
        // NOTE: All objects, allocated in Runtime, must have the DEFAULT_ALIGNMENT alignment.
        void *mem = tlab->Alloc(AlignUp(ALLOC_SIZE, DEFAULT_ALIGNMENT_IN_BYTES));
        if (mem != nullptr) {
            ASSERT_TRUE(ToUintPtr(mem) == oldFreePointer);
        } else {
            ASSERT_TRUE(*endAddr < (oldFreePointer + ALLOC_SIZE));
            overflow = true;
        }
    }
    ASSERT_EQ(overflow, true) << "Increase the size of alloc_count to get overflow";
}

TEST_F(TLABTest, AlignedAlloc)
{
    constexpr size_t ARRAY_SIZE = 1024;
    TLAB *tlab = CreateNewTLAB();
    Alignment align = DEFAULT_ALIGNMENT;
    std::array<int *, ARRAY_SIZE> arr {};

    size_t mask = GetAlignmentInBytes(align) - 1;

    // Allocations
    srand(seed_);
    for (size_t i = 0; i < ARRAY_SIZE; ++i) {
        arr[i] = static_cast<int *>(tlab->Alloc(sizeof(int)));
        // NOLINTNEXTLINE(cert-msc50-cpp)
        *arr[i] = rand() % std::numeric_limits<int>::max();
    }

    // Allocations checking
    srand(seed_);
    for (size_t i = 0; i < ARRAY_SIZE; ++i) {
        ASSERT_NE(arr[i], nullptr) << "value of i: " << i << ", align: " << align;
        ASSERT_EQ(reinterpret_cast<size_t>(arr[i]) & mask, static_cast<size_t>(0))
            << "value of i: " << i << ", align: " << align;
        // NOLINTNEXTLINE(cert-msc50-cpp)
        ASSERT_EQ(*arr[i], rand() % std::numeric_limits<int>::max()) << "value of i: " << i << ", align: " << align;
    }

    void *ptr = tlab->Alloc(TLAB_TEST_SIZE);
    ASSERT_EQ(ptr, nullptr) << "Here Alloc with allocation size = " << TLAB_TEST_SIZE << " bytes should return nullptr";
}

class TLABSharingTest : public testing::Test {
public:
    TLABSharingTest() : TLABSharingTest(CreateDefaultOptions()) {}

    explicit TLABSharingTest(const RuntimeOptions &options)
    {
        Runtime::Create(options);
    }

    ~TLABSharingTest() override
    {
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(TLABSharingTest);
    NO_MOVE_SEMANTIC(TLABSharingTest);

    static RuntimeOptions CreateDefaultOptions()
    {
        RuntimeOptions options;
        options.SetLoadRuntimes({"core"});
        // Must be gc with tlabs
        options.SetGcType("g1-gc");
        options.SetGcTriggerType("debug-never");
        options.SetShouldLoadBootPandaFiles(false);
        options.SetShouldInitializeIntrinsics(false);
        options.SetExplicitConcurrentGcEnabled(false);
        return options;
    }

    ManagedThread *StartNewMutator(PandaVM *vm)
    {
        auto *mtmt = MTManagedThread::Create(Runtime::GetCurrent(), vm);
        Mutator::SetCurrent(mtmt);
        mtmt->ManagedCodeBegin();
        return mtmt;
    }

    void StopAndDestroyMutator(ManagedThread *mt)
    {
        ASSERT(mt == Mutator::GetCurrent());
        mt->ManagedCodeEnd();
        MTManagedThread::CastFromMutator(mt)->Destroy();
    }

    ObjectHeader *AllocObj(size_t size)
    {
        size = size > sizeof(coretypes::String) ? size - sizeof(coretypes::String) : 0;
        return ObjectAllocator::AllocString(size);
    }
};

TEST_F(TLABSharingTest, AllocInSameThread)
{
    TLAB *tlabA;
    ManagedThread *mut1;
    ManagedThread *mut2;

    mut1 = ManagedThread::GetCurrent();

    {
        ScopedManagedCodeThread scope(mut1);
        auto *obj = AllocObj(0);
        tlabA = mut1->GetTLAB();
        ASSERT_TRUE(tlabA->ContainObject(obj));
        ASSERT_EQ(tlabA, mut1->GetTLABKeeper()->GetTLAB());
    }

    mut2 = StartNewMutator(mut1->GetVM());

    ASSERT_EQ(mut1->GetTLABKeeper(), mut2->GetTLABKeeper());

    ASSERT_TRUE(tlabA->ContainObject(ObjectAllocator::AllocString(0)));
    ASSERT_EQ(tlabA, mut2->GetTLAB());
    ASSERT_EQ(tlabA, mut1->GetTLABKeeper()->GetTLAB());

    StopAndDestroyMutator(mut2);
    Mutator::SetCurrent(mut1);
}

TEST_F(TLABSharingTest, AllocExceedTLABSize)
{
    TLAB *tlabA;
    TLAB *tlabB;
    ManagedThread *mut1;
    ManagedThread *mut2;

    mut1 = ManagedThread::GetCurrent();
    {
        ScopedManagedCodeThread scope(mut1);
        AllocObj(0);
        tlabA = mut1->GetTLAB();
    }

    mut2 = StartNewMutator(mut1->GetVM());
    AllocObj(0);

    constexpr size_t DESIRED_LEFT_SIZE = 80;
    constexpr size_t TOO_BIG_OBJECT = DESIRED_LEFT_SIZE * 2;
    constexpr size_t FITABLE_OBJECT = 80;
    ASSERT_TRUE(tlabA->ContainObject(AllocObj(tlabA->GetFreeSize() - DESIRED_LEFT_SIZE)));
    ASSERT_EQ(tlabA->GetFreeSize(), DESIRED_LEFT_SIZE);
    ASSERT_FALSE(tlabA->ContainObject(AllocObj(TOO_BIG_OBJECT)));

    tlabB = mut2->GetTLAB();

    ASSERT_EQ(tlabB, mut1->GetTLABKeeper()->GetTLAB());
    ASSERT_NE(tlabB, tlabA);
    ASSERT_EQ(tlabA, mut1->GetTLAB());

    mut2->ManagedCodeEnd();
    Mutator::SetCurrent(mut1);
    {
        ScopedManagedCodeThread scope(mut1);
        ASSERT_TRUE(tlabA->ContainObject(AllocObj(FITABLE_OBJECT)));
        ASSERT_TRUE(tlabB->ContainObject(AllocObj(TOO_BIG_OBJECT)));
    }
    Mutator::SetCurrent(mut2);
    mut2->ManagedCodeBegin();

    StopAndDestroyMutator(mut2);
    Mutator::SetCurrent(mut1);
}

TEST_F(TLABSharingTest, AllocInDifferentThreads)
{
    TLAB *tlab1;
    ManagedThread *mut1;

    mut1 = ManagedThread::GetCurrent();
    {
        ScopedManagedCodeThread scope(mut1);
        AllocObj(0);
        tlab1 = mut1->GetTLAB();
        ASSERT_EQ(tlab1, mut1->GetTLABKeeper()->GetTLAB());
    }

    std::thread([tlab1, mut1, this]() {
        TLAB *tlab2;
        ManagedThread *mut2 = StartNewMutator(mut1->GetVM());
        AllocObj(0);
        tlab2 = mut2->GetTLAB();
        ASSERT_EQ(tlab2, mut2->GetTLABKeeper()->GetTLAB());
        ASSERT_NE(tlab1, tlab2);
        ASSERT_NE(mut1->GetTLABKeeper(), mut2->GetTLABKeeper());
        StopAndDestroyMutator(mut2);
    }).join();
}

}  // namespace ark::mem
