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

#include "plugins/ets/runtime/ets_class_root.h"
#include "plugins/ets/runtime/ets_vm.h"
#include "plugins/ets/runtime/ets_coroutine.h"
#include "plugins/ets/runtime/tooling/helpers.h"
#include "plugins/ets/runtime/types/ets_primitives.h"
#include "runtime/include/tooling/vreg_value.h"

namespace ark::ets::test {

class VRegConversionTest : public testing::Test {
public:
    static void SetUpTestSuite()
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

    static void TearDownTestSuite()
    {
        Runtime::Destroy();
    }

    VRegConversionTest() = default;
    ~VRegConversionTest() override = default;

    NO_COPY_SEMANTIC(VRegConversionTest);
    NO_MOVE_SEMANTIC(VRegConversionTest);

    void SetUp() override
    {
        coroutine_ = EtsCoroutine::GetCurrent();
        coroutine_->ManagedCodeBegin();
    }

    void TearDown() override
    {
        coroutine_->ManagedCodeEnd();
    }

    static EtsExecutionContext *GetCtx()
    {
        return EtsCoroutine::GetCurrent()->GetExecutionCtx();
    }

private:
    EtsCoroutine *coroutine_ = nullptr;
};

TEST_F(VRegConversionTest, ReferenceTypeRoundTrip)
{
    auto *ctx = GetCtx();
    auto *cls = ctx->GetPandaVM()->GetClassLinker()->GetClassRoot(EtsClassRoot::OBJECT);
    if (cls == nullptr) {
        GTEST_SKIP() << "Object class root not available without boot panda files";
    }
    auto *obj = EtsObject::Create(cls);
    ASSERT_NE(obj, nullptr);

    ark::tooling::VRegValue vreg = tooling::EtsObjectToVRegValue(obj, "L");
    EXPECT_NE(vreg.GetValue(), 0);

    EtsObject *back = tooling::VRegValueToEtsObject(vreg, "L", ctx);
    EXPECT_EQ(back, obj);
}

TEST_F(VRegConversionTest, EmptySignatureDefaultsToReference)
{
    auto *ctx = GetCtx();
    auto *cls = ctx->GetPandaVM()->GetClassLinker()->GetClassRoot(EtsClassRoot::OBJECT);
    if (cls == nullptr) {
        GTEST_SKIP() << "Object class root not available without boot panda files";
    }
    auto *obj = EtsObject::Create(cls);
    ASSERT_NE(obj, nullptr);

    ark::tooling::VRegValue vreg = tooling::EtsObjectToVRegValue(obj, "");
    EXPECT_NE(vreg.GetValue(), 0);

    EtsObject *back = tooling::VRegValueToEtsObject(vreg, "", ctx);
    EXPECT_EQ(back, obj);
}

TEST_F(VRegConversionTest, ArraySignatureDefaultsToReference)
{
    auto *ctx = GetCtx();
    auto *cls = ctx->GetPandaVM()->GetClassLinker()->GetClassRoot(EtsClassRoot::OBJECT);
    if (cls == nullptr) {
        GTEST_SKIP() << "Object class root not available without boot panda files";
    }
    auto *obj = EtsObject::Create(cls);
    ASSERT_NE(obj, nullptr);

    ark::tooling::VRegValue vreg = tooling::EtsObjectToVRegValue(obj, "[");
    EXPECT_NE(vreg.GetValue(), 0);

    EtsObject *back = tooling::VRegValueToEtsObject(vreg, "[", ctx);
    EXPECT_EQ(back, obj);
}

TEST_F(VRegConversionTest, UnknownSignatureDefaultsToReference)
{
    auto *ctx = GetCtx();
    auto *cls = ctx->GetPandaVM()->GetClassLinker()->GetClassRoot(EtsClassRoot::OBJECT);
    if (cls == nullptr) {
        GTEST_SKIP() << "Object class root not available without boot panda files";
    }
    auto *obj = EtsObject::Create(cls);
    ASSERT_NE(obj, nullptr);

    ark::tooling::VRegValue vreg = tooling::EtsObjectToVRegValue(obj, "X");
    EXPECT_NE(vreg.GetValue(), 0);

    EtsObject *back = tooling::VRegValueToEtsObject(vreg, "X", ctx);
    EXPECT_EQ(back, obj);
}

TEST_F(VRegConversionTest, NullReferenceToVRegValue)
{
    ark::tooling::VRegValue vreg = tooling::EtsObjectToVRegValue(nullptr, "L");
    EXPECT_EQ(vreg.GetValue(), 0);

    ark::tooling::VRegValue vregEmpty = tooling::EtsObjectToVRegValue(nullptr, "");
    EXPECT_EQ(vregEmpty.GetValue(), 0);

    ark::tooling::VRegValue vregPrim = tooling::EtsObjectToVRegValue(nullptr, "I");
    EXPECT_EQ(vregPrim.GetValue(), 0);
}

TEST_F(VRegConversionTest, VRegValueZeroIsNullObject)
{
    auto *ctx = GetCtx();
    ark::tooling::VRegValue vreg(0);
    EtsObject *back = tooling::VRegValueToEtsObject(vreg, "L", ctx);
    EXPECT_EQ(back, nullptr);
}

TEST_F(VRegConversionTest, EtsValueToVRegValueNullObject)
{
    ark::tooling::VRegValue vreg = tooling::EtsObjectToVRegValue(nullptr, "L");
    EXPECT_EQ(vreg.GetValue(), 0);
}

TEST_F(VRegConversionTest, VRegValuePreservesReferenceAddress)
{
    auto *ctx = GetCtx();
    auto *cls = ctx->GetPandaVM()->GetClassLinker()->GetClassRoot(EtsClassRoot::OBJECT);
    if (cls == nullptr) {
        GTEST_SKIP() << "Object class root not available without boot panda files";
    }
    auto *obj = EtsObject::Create(cls);
    ASSERT_NE(obj, nullptr);

    ark::tooling::VRegValue vreg = tooling::EtsObjectToVRegValue(obj, "L");
    EXPECT_EQ(vreg.GetValue(), reinterpret_cast<int64_t>(obj->GetCoreType()));
}

TEST_F(VRegConversionTest, VRegValueRoundTripPreservesIdentity)
{
    auto *ctx = GetCtx();
    auto *cls = ctx->GetPandaVM()->GetClassLinker()->GetClassRoot(EtsClassRoot::OBJECT);
    if (cls == nullptr) {
        GTEST_SKIP() << "Object class root not available without boot panda files";
    }
    auto *obj = EtsObject::Create(cls);
    ASSERT_NE(obj, nullptr);

    ark::tooling::VRegValue vreg1 = tooling::EtsObjectToVRegValue(obj, "L");
    EtsObject *back1 = tooling::VRegValueToEtsObject(vreg1, "L", ctx);

    ark::tooling::VRegValue vreg2 = tooling::EtsObjectToVRegValue(back1, "L");
    EtsObject *back2 = tooling::VRegValueToEtsObject(vreg2, "L", ctx);

    EXPECT_EQ(back2, obj);
}

}  // namespace ark::ets::test
