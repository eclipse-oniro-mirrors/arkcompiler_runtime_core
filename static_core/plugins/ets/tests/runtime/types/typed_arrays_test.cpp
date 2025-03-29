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

#include "types/ets_class.h"
#include "types/ets_arraybuffer.h"
#include "tests/runtime/types/ets_test_mirror_classes.h"
#include "cross_values.h"

namespace ark::ets::test {
/// Typed arrays intrinsics depend on memory layout
class TypedArrayRelatedMemberOffsetTest : public testing::Test {
public:
    TypedArrayRelatedMemberOffsetTest()
    {
        RuntimeOptions options;
        options.SetShouldLoadBootPandaFiles(true);
        options.SetShouldInitializeIntrinsics(false);
        options.SetCompilerEnableJit(false);
        options.SetGcType("epsilon");
        options.SetLoadRuntimes({"ets"});

        auto stdlib = std::getenv("PANDA_STD_LIB");
        if (stdlib == nullptr) {
            std::cerr << "PANDA_STD_LIB env variable should be set and point to mock_stdlib.abc" << std::endl;
            std::abort();
        }
        options.SetBootPandaFiles({stdlib});

        Runtime::Create(options);
        EtsCoroutine *coroutine = EtsCoroutine::GetCurrent();
        vm_ = coroutine->GetPandaVM();
    }

    ~TypedArrayRelatedMemberOffsetTest() override
    {
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(TypedArrayRelatedMemberOffsetTest);
    NO_MOVE_SEMANTIC(TypedArrayRelatedMemberOffsetTest);

    static std::vector<MirrorFieldInfo> GetInt8ArrayMembers()
    {
        return std::vector<MirrorFieldInfo> {
            MirrorFieldInfo("buffer", ark::cross_values::GetInt8ArrayBufferOffset(RUNTIME_ARCH)),
            MirrorFieldInfo("lengthInt", ark::cross_values::GetInt8ArrayLengthOffset(RUNTIME_ARCH)),
            MirrorFieldInfo("byteOffset", ark::cross_values::GetInt8ArrayByteOffsetOffset(RUNTIME_ARCH)),
            MirrorFieldInfo("arrayBufferBacked", ark::cross_values::GetInt8ArrayArrayBufferBackedOffset(RUNTIME_ARCH))};
    }

    static std::vector<MirrorFieldInfo> GetArrayBufferMembers()
    {
        return std::vector<MirrorFieldInfo> {
            MirrorFieldInfo("data", ark::cross_values::GetArrayBufferDataOffset(RUNTIME_ARCH))};
    }

    static std::vector<MirrorFieldInfo> GetSharedArrayBufferMembers()
    {
        return std::vector<MirrorFieldInfo> {
            MirrorFieldInfo("sharedMemory", ark::cross_values::GetSharedArrayBufferSharedMemoryOffset(RUNTIME_ARCH))};
    }

    static std::vector<MirrorFieldInfo> GetSharedMemoryMembers()
    {
        return std::vector<MirrorFieldInfo> {
            MirrorFieldInfo("data", ark::cross_values::GetSharedMemoryDataOffset(RUNTIME_ARCH))};
    }

    EtsClass *GetClass(const char *name)
    {
        os::memory::WriteLockHolder lock(*vm_->GetMutatorLock());
        return vm_->GetClassLinker()->GetClass(name);
    }

protected:
    PandaEtsVM *vm_ = nullptr;  // NOLINT(misc-non-private-member-variables-in-classes)
};

TEST_F(TypedArrayRelatedMemberOffsetTest, Int8ArrayLayout)
{
    auto *klass = GetClass("Lescompat/Int8Array;");
    MirrorFieldInfo::CompareMemberOffsets(klass, GetInt8ArrayMembers(), false);
}

TEST_F(TypedArrayRelatedMemberOffsetTest, ArrayBufferLayout)
{
    auto *klass = GetClass("Lescompat/ArrayBuffer;");
    MirrorFieldInfo::CompareMemberOffsets(klass, GetArrayBufferMembers(), false);
}

TEST_F(TypedArrayRelatedMemberOffsetTest, SharedArrayBufferLayout)
{
    auto *klass = GetClass("Lescompat/SharedArrayBuffer;");
    MirrorFieldInfo::CompareMemberOffsets(klass, GetSharedArrayBufferMembers(), false);
}

TEST_F(TypedArrayRelatedMemberOffsetTest, SharedMemoryLayout)
{
    auto *klass = GetClass("Lescompat/SharedMemory;");
    MirrorFieldInfo::CompareMemberOffsets(klass, GetSharedMemoryMembers(), false);
}
}  // namespace ark::ets::test
