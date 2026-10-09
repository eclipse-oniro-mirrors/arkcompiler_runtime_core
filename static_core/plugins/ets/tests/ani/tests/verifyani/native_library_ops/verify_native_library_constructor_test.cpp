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

#include "plugins/ets/tests/ani/ani_gtest/verify_ani_gtest.h"

namespace ark::ets::ani::verify::testing {

namespace {

constexpr ani_int EXPECTED_VALUE = 42;

class ScopedReleasedFixtureNativeFrame {
public:
    ScopedReleasedFixtureNativeFrame(PandaAniEnv *pandaEnv, ani_env **env) : pandaEnv_(pandaEnv), env_(env) {}

    std::optional<PandaString> Release()
    {
        auto err = pandaEnv_->GetEnvANIVerifier()->PopNativeFrame();
        if (!err) {
            released_ = true;
            RefreshEnv();
        }
        return err;
    }

    void Restore()
    {
        if (!released_) {
            return;
        }
        pandaEnv_->GetEnvANIVerifier()->PushNativeFrame(pandaEnv_);
        released_ = false;
        RefreshEnv();
    }

    ~ScopedReleasedFixtureNativeFrame()
    {
        Restore();
    }

    NO_COPY_SEMANTIC(ScopedReleasedFixtureNativeFrame);
    NO_MOVE_SEMANTIC(ScopedReleasedFixtureNativeFrame);

private:
    void RefreshEnv()
    {
        *env_ = pandaEnv_->GetEnvANIVerifier()->GetEnv();
    }

    PandaAniEnv *pandaEnv_ {};
    ani_env **env_ {};
    bool released_ {};
};

}  // namespace

class NativeLibraryConstructorVerifyTest : public VerifyAniTest {};

TEST_F(NativeLibraryConstructorVerifyTest, local_refs_created_in_ani_constructor_are_scoped_with_raw_env)
{
    auto *pandaEnv = EtsExecutionContext::GetCurrent()->GetPandaAniEnv();
    ScopedReleasedFixtureNativeFrame releasedFrame(pandaEnv, &env_);
    auto popErr = releasedFrame.Release();
    ASSERT_FALSE(popErr.has_value()) << popErr.value();

    ASSERT_TRUE(pandaEnv->GetEtsVM()->LoadNativeLibrary(pandaEnv, "libverify_ani_ctor_local_scope_test.so", false, ""));
    ASSERT_NO_ABORT_MESSAGE();

    releasedFrame.Restore();

    auto object =
        CallEtsFunction<ani_ref>("verify_native_library_constructor_test", "new_VerifyAniCtorLocalScopeClass");
    ASSERT_NE(object, nullptr);

    ani_boolean unhandledError {};
    ASSERT_EQ(env_->ExistUnhandledError(&unhandledError), ANI_OK);
    ASSERT_EQ(unhandledError, ANI_FALSE);
    ASSERT_NO_ABORT_MESSAGE();

    auto value =
        CallEtsFunction<ani_int>("verify_native_library_constructor_test", "getValue", static_cast<ani_object>(object));
    ASSERT_EQ(value, EXPECTED_VALUE);
    ASSERT_NO_ABORT_MESSAGE();
}

}  // namespace ark::ets::ani::verify::testing
