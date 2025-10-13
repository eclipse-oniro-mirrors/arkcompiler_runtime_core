/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "ets_coroutine.h"
#include "ets_vm.h"

#include "plugins/ets/runtime/types/ets_reflect_field.h"
#include "plugins/ets/runtime/types/ets_reflect_method.h"
#include "tests/runtime/types/ets_test_mirror_classes.h"

namespace ark::ets::test {

class EtsReflectTest : public testing::Test {
public:
    EtsReflectTest()
    {
        options_.SetShouldLoadBootPandaFiles(true);
        options_.SetShouldInitializeIntrinsics(true);
        options_.SetCompilerEnableJit(false);
        options_.SetGcType("g1-gc");
        options_.SetLoadRuntimes({"ets"});

        auto stdlib = std::getenv("PANDA_STD_LIB");
        if (stdlib == nullptr) {
            std::cerr << "PANDA_STD_LIB env variable should be set and point to mock_stdlib.abc" << std::endl;
            std::abort();
        }
        options_.SetBootPandaFiles({stdlib});

        Runtime::Create(options_);
    }

    ~EtsReflectTest() override
    {
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(EtsReflectTest);
    NO_MOVE_SEMANTIC(EtsReflectTest);

    void SetUp() override
    {
        coroutine_ = EtsCoroutine::GetCurrent();
        vm_ = coroutine_->GetPandaVM();
        coroutine_->ManagedCodeBegin();
    }

    void TearDown() override
    {
        coroutine_->ManagedCodeEnd();
    }

    static std::vector<MirrorFieldInfo> GetReflectMethodClassMembers()
    {
        return std::vector<MirrorFieldInfo> {MIRROR_FIELD_INFO(EtsReflectMethod, name_, "name"),
                                             MIRROR_FIELD_INFO(EtsReflectMethod, ownerType_, "ownerType"),
                                             MIRROR_FIELD_INFO(EtsReflectMethod, attr_, "attributes"),
                                             MIRROR_FIELD_INFO(EtsReflectMethod, etsMethod_, "methodPtr"),
                                             MIRROR_FIELD_INFO(EtsReflectMethod, accessMod_, "accessMod")};
    }

    static std::vector<MirrorFieldInfo> GetReflectFieldClassMembers()
    {
        return std::vector<MirrorFieldInfo> {MIRROR_FIELD_INFO(EtsReflectField, name_, "name"),
                                             MIRROR_FIELD_INFO(EtsReflectField, fieldType_, "fieldType"),
                                             MIRROR_FIELD_INFO(EtsReflectField, ownerType_, "ownerType"),
                                             MIRROR_FIELD_INFO(EtsReflectField, attr_, "attributes"),
                                             MIRROR_FIELD_INFO(EtsReflectField, etsField_, "fieldPtr"),
                                             MIRROR_FIELD_INFO(EtsReflectField, accessMod_, "accessMod")};
    }

protected:
    PandaEtsVM *vm_ = nullptr;  // NOLINT(misc-non-private-member-variables-in-classes)

private:
    RuntimeOptions options_;
    EtsCoroutine *coroutine_ = nullptr;
};

TEST_F(EtsReflectTest, ReflectFieldMemoryLayout)
{
    auto *reflectInstanceFieldClass = PlatformTypes(vm_)->reflectInstanceField;
    auto *reflectStaticFieldClass = PlatformTypes(vm_)->reflectStaticField;
    MirrorFieldInfo::CompareMemberOffsets(reflectInstanceFieldClass, GetReflectFieldClassMembers(), false);
    MirrorFieldInfo::CompareMemberOffsets(reflectStaticFieldClass, GetReflectFieldClassMembers(), false);
}

TEST_F(EtsReflectTest, ReflectMethodMemoryLayout)
{
    auto *reflectInstanceMethodClass = PlatformTypes(vm_)->reflectInstanceMethod;
    auto *reflectStaticMethodClass = PlatformTypes(vm_)->reflectStaticMethod;
    auto *reflectConstructorClass = PlatformTypes(vm_)->reflectConstructor;
    MirrorFieldInfo::CompareMemberOffsets(reflectInstanceMethodClass, GetReflectMethodClassMembers(), false);
    MirrorFieldInfo::CompareMemberOffsets(reflectStaticMethodClass, GetReflectMethodClassMembers(), false);
    MirrorFieldInfo::CompareMemberOffsets(reflectConstructorClass, GetReflectMethodClassMembers(), false);
}

}  // namespace ark::ets::test
