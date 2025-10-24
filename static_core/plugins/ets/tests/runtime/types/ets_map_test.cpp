/**
 * Copyright (c) 2021-2025 Huawei Device Co., Ltd.
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
#include "types/ets_map.h"

#include "tests/runtime/types/ets_test_mirror_classes.h"

namespace ark::ets::test {

class EtsMapTest : public testing::Test {
public:
    EtsMapTest()
    {
        RuntimeOptions options;
        options.SetShouldLoadBootPandaFiles(true);
        options.SetShouldInitializeIntrinsics(true);
        options.SetCompilerEnableJit(false);
        options.SetGcType("epsilon");
        options.SetLoadRuntimes({"ets"});

        auto stdlib = std::getenv("PANDA_STD_LIB");
        if (stdlib == nullptr) {
            std::cerr << "PANDA_STD_LIB env variable should be set and point to mock_stdlib.abc" << std::endl;
            std::abort();  // CC-OFF(G.STD.16-CPP) fatal error
        }
        options.SetBootPandaFiles({stdlib});

        Runtime::Create(options);
        EtsCoroutine *coroutine = EtsCoroutine::GetCurrent();
        vm_ = coroutine->GetPandaVM();
    }

    ~EtsMapTest() override
    {
        Runtime::Destroy();
    }

    NO_COPY_SEMANTIC(EtsMapTest);
    NO_MOVE_SEMANTIC(EtsMapTest);

    static std::vector<MirrorFieldInfo> GetMapMembers()
    {
        return std::vector<MirrorFieldInfo> {MIRROR_FIELD_INFO(EtsEscompatMap, data_, "data"),
                                             MIRROR_FIELD_INFO(EtsEscompatMap, buckets_, "buckets"),
                                             MIRROR_FIELD_INFO(EtsEscompatMap, cap_, "cap"),
                                             MIRROR_FIELD_INFO(EtsEscompatMap, numEntries_, "numEntries"),
                                             MIRROR_FIELD_INFO(EtsEscompatMap, sizeVal_, "sizeVal"),
                                             MIRROR_FIELD_INFO(EtsEscompatMap, initialCapacity_, "initialCapacity")};
    }

    static std::vector<MirrorFieldInfo> GetMapEntryMembers()
    {
        return std::vector<MirrorFieldInfo> {MIRROR_FIELD_INFO(EtsEscompatMapEntry, key_, "key"),
                                             MIRROR_FIELD_INFO(EtsEscompatMapEntry, val_, "val"),
                                             MIRROR_FIELD_INFO(EtsEscompatMapEntry, next_, "next")};
    }

protected:
    PandaEtsVM *vm_ = nullptr;  // NOLINT(misc-non-private-member-variables-in-classes)
};

// Check both EtsEscompatMap and ark::Class<Map> has the same number of fields and at the same offsets
TEST_F(EtsMapTest, MapMemoryLayout)
{
    EtsClass *mapClass = PlatformTypes(vm_)->coreMap;
    MirrorFieldInfo::CompareMemberOffsets(mapClass, GetMapMembers());
}

// Check both EtsEscompatMapEntry and ark::Class<MapEntry> has the same number of fields and at the same offsets
TEST_F(EtsMapTest, MapEntryMemoryLayout)
{
    EtsClass *mapEntryClass = PlatformTypes(vm_)->coreMapEntry;
    MirrorFieldInfo::CompareMemberOffsets(mapEntryClass, GetMapEntryMembers());
}

}  // namespace ark::ets::test
