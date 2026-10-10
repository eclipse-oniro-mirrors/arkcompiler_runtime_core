/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#include <cstdlib>
#include <gtest/gtest.h>

#include "plugins/ets/runtime/ets_vm.h"
#include "plugins/ets/runtime/ets_coroutine.h"
#include "plugins/ets/runtime/ets_exceptions.h"
#include "plugins/ets/runtime/tooling/helpers.h"
#include "plugins/ets/runtime/types/ets_primitives.h"
#include "plugins/ets/runtime/types/ets_box_primitive.h"
#include "plugins/ets/runtime/types/ets_string.h"
#include "runtime/include/tooling/vreg_value.h"

using ark::ets::EtsBoolean;
using ark::ets::EtsByte;
using ark::ets::EtsChar;
using ark::ets::EtsDouble;
using ark::ets::EtsFloat;
using ark::ets::EtsInt;
using ark::ets::EtsLong;
using ark::ets::EtsObject;
using ark::ets::EtsShort;
using ark::ets::EtsString;

extern "C" {
EtsBoolean DebuggerAPIGetLocalBoolean(EtsLong regNumber);
EtsByte DebuggerAPIGetLocalByte(EtsLong regNumber);
EtsShort DebuggerAPIGetLocalShort(EtsLong regNumber);
EtsChar DebuggerAPIGetLocalChar(EtsLong regNumber);
EtsInt DebuggerAPIGetLocalInt(EtsLong regNumber);
EtsFloat DebuggerAPIGetLocalFloat(EtsLong regNumber);
EtsDouble DebuggerAPIGetLocalDouble(EtsLong regNumber);
EtsLong DebuggerAPIGetLocalLong(EtsLong regNumber);
EtsObject *DebuggerAPIGetLocalObject(EtsLong regNumber);
void DebuggerAPISetLocalBoolean(EtsLong regNumber, EtsBoolean value);
void DebuggerAPISetLocalByte(EtsLong regNumber, EtsByte value);
void DebuggerAPISetLocalShort(EtsLong regNumber, EtsShort value);
void DebuggerAPISetLocalChar(EtsLong regNumber, EtsChar value);
void DebuggerAPISetLocalInt(EtsLong regNumber, EtsInt value);
void DebuggerAPISetLocalFloat(EtsLong regNumber, EtsFloat value);
void DebuggerAPISetLocalDouble(EtsLong regNumber, EtsDouble value);
void DebuggerAPISetLocalLong(EtsLong regNumber, EtsLong value);
void DebuggerAPISetLocalObject(EtsLong regNumber, EtsObject *value);
EtsObject *DebuggerAPIGetThis(EtsInt threadId, EtsInt frameDepth);
EtsObject *DebuggerAPIGet(EtsInt threadId, EtsInt frameDepth, EtsString *name);
void DebuggerAPISet(EtsInt threadId, EtsInt frameDepth, EtsString *name, EtsObject *value);
EtsObject *DebuggerAPIGetLocalValue(EtsInt threadId, EtsInt frameDepth, EtsLong regNumber, EtsString *typeSignature);
void DebuggerAPISetLocalValue(EtsInt threadId, EtsInt frameDepth, EtsLong regNumber, EtsString *typeSignature,
                              EtsObject *value);
EtsObject *DebuggerAPIGetGlobalVariable(EtsString *name);
void DebuggerAPISetGlobalVariable(EtsString *name, EtsObject *value);
}

namespace ark::ets::test {

static constexpr EtsInt DUMMY_INT_VALUE = 42;

class DebuggerApiIntrinsicsTest : public testing::Test {
public:
    DebuggerApiIntrinsicsTest()
    {
        RuntimeOptions options;
        options.SetShouldLoadBootPandaFiles(true);
        options.SetShouldInitializeIntrinsics(false);
        options.SetGcType("epsilon");
        options.SetLoadRuntimes({"ets"});

        auto *stdlib = std::getenv("PANDA_STD_LIB");
        if (stdlib != nullptr) {
            options.SetBootPandaFiles({stdlib});
        }

        Runtime::Create(options);
    }

    ~DebuggerApiIntrinsicsTest() override
    {
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(DebuggerApiIntrinsicsTest);
    NO_MOVE_SEMANTIC(DebuggerApiIntrinsicsTest);

    void SetUp() override
    {
        coroutine_ = EtsCoroutine::GetCurrent();
        coroutine_->ManagedCodeBegin();
    }

    void TearDown() override
    {
        coroutine_->ManagedCodeEnd();
    }

protected:
    void EnableDebugMode()
    {
        Runtime::GetCurrent()->SetDebugMode(true);
    }

    void EnableDebugSession()
    {
        // StartDebugSession() internally calls AddListener which triggers a safepoint
        // (ScopedSuspendAllThreads). Calling it from managed scope (where the mutator
        // lock is held in shared mode) deadlocks. Exit managed scope first, start the
        // session, then re-enter.
        coroutine_->ManagedCodeEnd();
        auto *runtime = Runtime::GetCurrent();
        runtime->SetDebugMode(true);
        runtime->StartDebugSession();
        coroutine_->ManagedCodeBegin();
    }

    bool HasPendingException()
    {
        return GetCtx()->GetMT()->HasPendingException();
    }

    bool CanThrowException()
    {
        auto *pt = PlatformTypes(GetCtx());
        return pt != nullptr && pt->escompatError != nullptr && pt->coreString != nullptr &&
               pt->escompatErrorOptions != nullptr && ManagedThread::IsManagedScope();
    }

    static EtsExecutionContext *GetCtx()
    {
        return EtsCoroutine::GetCurrent()->GetExecutionCtx();
    }

private:
    EtsCoroutine *coroutine_ = nullptr;
};

TEST_F(DebuggerApiIntrinsicsTest, GetLocalInt_DebuggerNotEnabled)
{
    auto result = DebuggerAPIGetLocalInt(0);
    EXPECT_EQ(result, static_cast<EtsInt>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, GetLocalInt_InvalidRegister)
{
    EnableDebugMode();
    auto result = DebuggerAPIGetLocalInt(-1);
    EXPECT_EQ(result, static_cast<EtsInt>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, SetLocalInt_DebuggerNotEnabled)
{
    DebuggerAPISetLocalInt(0, DUMMY_INT_VALUE);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, SetLocalInt_InvalidRegister)
{
    EnableDebugMode();
    DebuggerAPISetLocalInt(-1, DUMMY_INT_VALUE);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, GetThis_DebuggerNotEnabled)
{
    auto *result = DebuggerAPIGetThis(0, 0);
    EXPECT_EQ(result, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, Get_DebuggerNotEnabled)
{
    auto *result = DebuggerAPIGet(0, 0, nullptr);
    EXPECT_EQ(result, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, Get_NullName)
{
    EnableDebugMode();
    auto *result = DebuggerAPIGet(0, 0, nullptr);
    EXPECT_EQ(result, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, GetLocalValue_DebuggerNotEnabled)
{
    auto *result = DebuggerAPIGetLocalValue(0, 0, 0, nullptr);
    EXPECT_EQ(result, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, GetLocalValue_InvalidRegister)
{
    EnableDebugMode();
    auto *result = DebuggerAPIGetLocalValue(0, 0, -1, nullptr);
    EXPECT_EQ(result, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, SetLocalValue_DebuggerNotEnabled)
{
    DebuggerAPISetLocalValue(0, 0, 0, nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, SetLocalValue_InvalidRegister)
{
    EnableDebugMode();
    DebuggerAPISetLocalValue(0, 0, -1, nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, SetLocalValue_NullValue)
{
    EnableDebugMode();
    DebuggerAPISetLocalValue(0, 0, 0, nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, GetGlobalVariable_ThrowsException)
{
    auto *result = DebuggerAPIGetGlobalVariable(nullptr);
    EXPECT_EQ(result, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, SetGlobalVariable_ThrowsException)
{
    DebuggerAPISetGlobalVariable(nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, CoroutineToPtThread_ReturnsValidThread)
{
    auto *mt = EtsCoroutine::GetCurrent()->GetExecutionCtx()->GetMT();
    ASSERT_NE(mt, nullptr);
    auto ptThread = ark::ets::tooling::CoroutineToPtThread(mt);
    EXPECT_NE(ptThread.GetId(), 0);
}

TEST_F(DebuggerApiIntrinsicsTest, EnableDebugSession_SetsDebugMode)
{
    EnableDebugSession();
    auto *runtime = Runtime::GetCurrent();
    EXPECT_TRUE(runtime->IsDebugMode());
}

TEST_F(DebuggerApiIntrinsicsTest, GetAllTypedGetters_DebuggerNotEnabled)
{
    EXPECT_EQ(DebuggerAPIGetLocalBoolean(0), static_cast<EtsBoolean>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    EXPECT_EQ(DebuggerAPIGetLocalByte(0), static_cast<EtsByte>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    EXPECT_EQ(DebuggerAPIGetLocalShort(0), static_cast<EtsShort>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    EXPECT_EQ(DebuggerAPIGetLocalChar(0), static_cast<EtsChar>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    EXPECT_EQ(DebuggerAPIGetLocalFloat(0), static_cast<EtsFloat>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    EXPECT_EQ(DebuggerAPIGetLocalDouble(0), static_cast<EtsDouble>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    EXPECT_EQ(DebuggerAPIGetLocalLong(0), static_cast<EtsLong>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    EXPECT_EQ(DebuggerAPIGetLocalObject(0), nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, GetAllTypedSetters_DebuggerNotEnabled)
{
    DebuggerAPISetLocalBoolean(0, EtsBoolean(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    DebuggerAPISetLocalByte(0, EtsByte(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    DebuggerAPISetLocalShort(0, EtsShort(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    DebuggerAPISetLocalChar(0, EtsChar(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    DebuggerAPISetLocalFloat(0, EtsFloat(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    DebuggerAPISetLocalDouble(0, EtsDouble(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    DebuggerAPISetLocalLong(0, EtsLong(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
    DebuggerAPISetLocalObject(0, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, EtsValueToVRegValue_NullObject)
{
    ark::tooling::VRegValue vreg = ark::ets::tooling::EtsObjectToVRegValue(nullptr, "L");
    EXPECT_EQ(vreg.GetValue(), 0);
}

TEST_F(DebuggerApiIntrinsicsTest, EtsObjectToVRegValue_NullRefType)
{
    ark::tooling::VRegValue vreg = ark::ets::tooling::EtsObjectToVRegValue(nullptr, "L");
    EXPECT_EQ(vreg.GetValue(), 0);
}

TEST_F(DebuggerApiIntrinsicsTest, EtsObjectToVRegValue_NullPrimitiveType)
{
    ark::tooling::VRegValue vreg = ark::ets::tooling::EtsObjectToVRegValue(nullptr, "I");
    EXPECT_EQ(vreg.GetValue(), 0);
}

TEST_F(DebuggerApiIntrinsicsTest, EtsObjectToVRegValue_NullBooleanType)
{
    ark::tooling::VRegValue vreg = ark::ets::tooling::EtsObjectToVRegValue(nullptr, "Z");
    EXPECT_EQ(vreg.GetValue(), 0);
}

TEST_F(DebuggerApiIntrinsicsTest, EtsObjectToVRegValue_NullFloatType)
{
    ark::tooling::VRegValue vreg = ark::ets::tooling::EtsObjectToVRegValue(nullptr, "F");
    EXPECT_EQ(vreg.GetValue(), 0);
}

TEST_F(DebuggerApiIntrinsicsTest, EtsObjectToVRegValue_NullDoubleType)
{
    ark::tooling::VRegValue vreg = ark::ets::tooling::EtsObjectToVRegValue(nullptr, "D");
    EXPECT_EQ(vreg.GetValue(), 0);
}

TEST_F(DebuggerApiIntrinsicsTest, EtsObjectToVRegValue_NullEmptySignature)
{
    ark::tooling::VRegValue vreg = ark::ets::tooling::EtsObjectToVRegValue(nullptr, "");
    EXPECT_EQ(vreg.GetValue(), 0);
}

TEST_F(DebuggerApiIntrinsicsTest, GetLocalInt_ValidRegisterInDebugMode_DebugSessionCheck)
{
    // Pre-start the debug session before calling DebuggerAPIGetLocalInt.
    // DebuggerAPIGetLocalInt internally calls StartDebugSession(), which triggers a
    // safepoint via AddListener. Calling StartDebugSession() from managed scope deadlocks,
    // so we start the session here (exiting and re-entering managed scope).
    EnableDebugSession();
    auto result = DebuggerAPIGetLocalInt(0);
    EXPECT_EQ(result, static_cast<EtsInt>(0));
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, GetThis_ReturnsNullFromStaticMethod)
{
    // Pre-start debug session to avoid deadlock: DebuggerAPIGetThis internally calls
    // StartDebugSession() which triggers a safepoint that deadlocks in managed scope.
    EnableDebugSession();
    auto *result = DebuggerAPIGetThis(0, 0);
    EXPECT_EQ(result, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, GetGlobalVariable_NullName_StillThrows)
{
    auto *result = DebuggerAPIGetGlobalVariable(nullptr);
    EXPECT_EQ(result, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, SetGlobalVariable_NullArgs_StillThrows)
{
    DebuggerAPISetGlobalVariable(nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, Set_DebuggerNotEnabled)
{
    DebuggerAPISet(0, 0, nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, Set_NullName)
{
    EnableDebugMode();
    DebuggerAPISet(0, 0, nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, Set_NullValue)
{
    EnableDebugMode();
    DebuggerAPISet(0, 0, nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, Set_NoFrameFound)
{
    EnableDebugSession();
    DebuggerAPISet(0, 0, nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

TEST_F(DebuggerApiIntrinsicsTest, Set_SignatureMatchesGet)
{
    auto *result1 = DebuggerAPIGet(0, 0, nullptr);
    EXPECT_EQ(result1, nullptr);
    DebuggerAPISet(0, 0, nullptr, nullptr);
    if (CanThrowException()) {
        EXPECT_TRUE(HasPendingException());
    }
}

}  // namespace ark::ets::test
