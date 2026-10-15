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

#include <cstdint>

#include "libarkbase/utils/cframe_layout.h"
#include "runtime/compiler.h"
#include "runtime/include/cframe.h"
#include "runtime/include/stack_walker.h"
#include "runtime/interpreter/frame.h"
#include "runtime/tests/test_utils.h"

namespace ark::test {

class FrameTest : public testing::Test {
public:
    FrameTest()
    {
        RuntimeOptions options;
        options.SetShouldLoadBootPandaFiles(false);
        options.SetShouldInitializeIntrinsics(false);
        // this test doesn't check GC logic - just to make test easier without any handles
        options.SetGcType("epsilon");
        Runtime::Create(options);
        thread_ = ark::MTManagedThread::GetCurrent();
        thread_->ManagedCodeBegin();
    }

    ~FrameTest() override
    {
        thread_->ManagedCodeEnd();
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(FrameTest);
    NO_MOVE_SEMANTIC(FrameTest);

private:
    ark::MTManagedThread *thread_;
};

template <bool IS_DYNAMIC = false>
Frame *CreateFrame(size_t nregs, Method *method, Frame *prev)
{
    uint32_t extSz = CORE_EXT_FRAME_DATA_SIZE;
    void *mem = aligned_alloc(8, ark::Frame::GetAllocSize(Frame::GetActualSize<IS_DYNAMIC>(nregs), extSz));
    return new (Frame::FromExt(mem, extSz)) ark::Frame(mem, method, prev, nregs);
}

void FreeFrame(Frame *f)
{
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
    std::free(f->GetExt());
}

TEST_F(FrameTest, Test)
{
    Frame *f = ark::test::CreateFrame(2, nullptr, nullptr);
    auto frameHandler = StaticFrameHandler(f);
    frameHandler.GetVReg(0).SetReference(nullptr);
    EXPECT_TRUE(frameHandler.GetVReg(0).HasObject());
    frameHandler.GetVReg(0).SetPrimitive(0);
    EXPECT_FALSE(frameHandler.GetVReg(0).HasObject());

    // NOLINTNEXTLINE(readability-magic-numbers)
    int64_t v64 = 0x1122334455667788;
    frameHandler.GetVReg(0).SetPrimitive(v64);
    EXPECT_EQ(frameHandler.GetVReg(0).GetLong(), v64);
    EXPECT_EQ(frameHandler.GetVReg(0).GetAs<int64_t>(), v64);

    frameHandler.GetVReg(1).MovePrimitive(frameHandler.GetVReg(0));
    EXPECT_FALSE(frameHandler.GetVReg(0).HasObject());
    EXPECT_EQ(frameHandler.GetVReg(0).Get(), static_cast<int32_t>(v64));

    frameHandler.GetVReg(1).MovePrimitive(frameHandler.GetVReg(0));
    EXPECT_FALSE(frameHandler.GetVReg(0).HasObject());
    EXPECT_EQ(frameHandler.GetVReg(0).GetLong(), v64);

    // NOLINTNEXTLINE(readability-magic-numbers)
    ObjectHeader *ptr = ark::mem::AllocateNullifiedPayloadString(15);
    frameHandler.GetVReg(0).SetReference(ptr);
    frameHandler.GetVReg(1).MoveReference(frameHandler.GetVReg(0));
    EXPECT_TRUE(frameHandler.GetVReg(0).HasObject());
    EXPECT_EQ(frameHandler.GetVReg(0).GetReference(), ptr);

    // NOLINTNEXTLINE(readability-magic-numbers)
    int32_t v32 = 0x11223344;
    frameHandler.GetVReg(0).SetPrimitive(v32);
    EXPECT_EQ(frameHandler.GetVReg(0).Get(), v32);
    EXPECT_EQ(frameHandler.GetVReg(0).GetAs<int32_t>(), v32);

    // NOLINTNEXTLINE(readability-magic-numbers)
    int16_t v16 = 0x1122;
    frameHandler.GetVReg(0).SetPrimitive(v16);
    EXPECT_EQ(frameHandler.GetVReg(0).Get(), v16);
    EXPECT_EQ(frameHandler.GetVReg(0).GetAs<int32_t>(), v16);

    // NOLINTNEXTLINE(readability-magic-numbers)
    int8_t v8 = 0x11;
    frameHandler.GetVReg(0).SetPrimitive(v8);
    EXPECT_EQ(frameHandler.GetVReg(0).Get(), v8);
    EXPECT_EQ(frameHandler.GetVReg(0).GetAs<int32_t>(), v8);

    // NOLINTNEXTLINE(readability-magic-numbers)
    float f32 = 123.5;
    frameHandler.GetVReg(0).SetPrimitive(f32);
    EXPECT_EQ(frameHandler.GetVReg(0).GetFloat(), f32);
    EXPECT_EQ(frameHandler.GetVReg(0).GetAs<float>(), f32);

    // NOLINTNEXTLINE(readability-magic-numbers)
    double f64 = 456.7;
    frameHandler.GetVReg(0).SetPrimitive(f64);
    EXPECT_EQ(frameHandler.GetVReg(0).GetDouble(), f64);
    EXPECT_EQ(frameHandler.GetVReg(0).GetAs<double>(), f64);

    ark::test::FreeFrame(f);
}

// The c2i bridge assembles the boundary frame as a standard FP frame
// (see cframe_layout.h and runtime/bridge/arch/*/compiled_code_to_interpreter_bridge*.S):
//   [fp+0] = saved_fp, [fp+1 slot] = lr, [fp-1 slot] = boundary marker.
// Pin the three slots so a layout regression fails fast.
TEST(BoundaryFrameTest, C2IBoundaryFrameThreeSlotLayout)
{
    using Boundary = BoundaryFrame<FrameKind::INTERPRETER>;
    static_assert(Boundary::FP_OFFSET == 0);
    static_assert(Boundary::RETURN_OFFSET == 1);
    static_assert(Boundary::METHOD_OFFSET == -1);

    EXPECT_EQ(Boundary::FP_OFFSET, 0);
    EXPECT_EQ(Boundary::RETURN_OFFSET, 1);
    EXPECT_EQ(Boundary::METHOD_OFFSET, -1);

    // Build the boundary frame exactly like Method::InvokeEntry (method-inl.h):
    // v[0] = callee regs, v[1] = marker, v[2] = saved_fp, v[3] = lr
    constexpr size_t FP_INDEX = 2;
    // NOLINTNEXTLINE(readability-magic-numbers)
    constexpr uintptr_t SAVED_FP = 0XDEADBEEF;
    // NOLINTNEXTLINE(readability-magic-numbers)
    constexpr uintptr_t LR = 0XCAFEBABE;
    C2IBridge bridge = {0, COMPILED_CODE_TO_INTERPRETER, SAVED_FP, LR};
    void *boundary = &bridge.v[FP_INDEX];
    // Read the slots the way an fp unwinder does
    auto *slots = reinterpret_cast<uintptr_t *>(boundary);

    EXPECT_TRUE(StackWalker::IsBoundaryFrame<FrameKind::INTERPRETER>(boundary));  // [fp-8] == marker
    EXPECT_EQ(slots[Boundary::FP_OFFSET], SAVED_FP);                              // [fp+0] == saved_fp
    EXPECT_EQ(slots[Boundary::RETURN_OFFSET], LR);                                // [fp+8] == lr
}

}  // namespace ark::test
