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

#include "app/document_loader.h"
#include "core/index/file_range_locator.h"

#include <gtest/gtest.h>

#include <utility>

namespace panda::bytecode_explorer::test {

TEST(FileRangeLocatorTest, ChoosesTheSmallestContainingRangeThenPrimaryThenStableId)
{
    StructureIndex structure;
    EntityNode file(1U, INVALID_ENTITY_ID, EntityKind::FILE, "sample.abc");
    file.primaryRange = FileRange {0U, 1024U};
    ASSERT_TRUE(structure.Add(std::move(file)));

    EntityNode broad(2U, 1U, EntityKind::CLASS, "Broad");
    broad.primaryRange = FileRange {100U, 100U};
    ASSERT_TRUE(structure.Add(std::move(broad)));

    EntityNode backing(3U, 1U, EntityKind::METHOD, "Backing");
    backing.backing.push_back({PhysicalEntityKind::CODE, {120U, 20U}, "code"});
    ASSERT_TRUE(structure.Add(std::move(backing)));

    EntityNode primary(4U, 1U, EntityKind::METHOD, "Primary");
    primary.primaryRange = FileRange {120U, 20U};
    ASSERT_TRUE(structure.Add(std::move(primary)));

    const auto locator = FileRangeLocator::Build(structure, 1024U);
    const auto match = locator->FindMostSpecific(125U);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->entityId, 4U);
    EXPECT_EQ(match->source, FileRangeSource::PRIMARY);
    EXPECT_EQ(match->range.offset, 120U);
    EXPECT_EQ(match->range.size, 20U);
    EXPECT_FALSE(locator->FindMostSpecific(1024U).has_value());
}

TEST(FileRangeLocatorTest, PreservesBackingIndexAndHalfOpenEnd)
{
    StructureIndex structure;
    EntityNode file(1U, INVALID_ENTITY_ID, EntityKind::FILE, "sample.abc");
    ASSERT_TRUE(structure.Add(std::move(file)));

    EntityNode method(2U, 1U, EntityKind::METHOD, "Method");
    method.backing.push_back({PhysicalEntityKind::METHOD, {100U, 4U}, "method"});
    method.backing.push_back({PhysicalEntityKind::CODE, {200U, 8U}, "code"});
    ASSERT_TRUE(structure.Add(std::move(method)));

    const auto locator = FileRangeLocator::Build(structure, 1024U);
    for (const uint64_t offset : {200U, 207U}) {
        const auto match = locator->FindMostSpecific(offset);
        ASSERT_TRUE(match.has_value());
        EXPECT_EQ(match->entityId, 2U);
        EXPECT_EQ(match->source, FileRangeSource::BACKING);
        ASSERT_TRUE(match->backingIndex.has_value());
        EXPECT_EQ(*match->backingIndex, 1U);
    }
    EXPECT_FALSE(locator->FindMostSpecific(208U).has_value());
}

TEST(FileRangeLocatorTest, IgnoresZeroLengthAndOutOfFileRanges)
{
    StructureIndex structure;
    EntityNode file(1U, INVALID_ENTITY_ID, EntityKind::FILE, "sample.abc");
    file.primaryRange = FileRange {10U, 0U};
    file.backing.push_back({PhysicalEntityKind::FILE, {1000U, 25U}, "file"});
    ASSERT_TRUE(structure.Add(std::move(file)));

    const auto locator = FileRangeLocator::Build(structure, 1024U);
    EXPECT_FALSE(locator->FindMostSpecific(10U).has_value());
    EXPECT_FALSE(locator->FindMostSpecific(1000U).has_value());

    StructureIndex empty;
    const auto emptyLocator = FileRangeLocator::Build(empty, 1024U);
    EXPECT_FALSE(emptyLocator->FindMostSpecific(0U).has_value());
}

TEST(FileRangeLocatorTest, BreaksEqualPrimaryTiesByEntityId)
{
    StructureIndex structure;
    EntityNode file(1U, INVALID_ENTITY_ID, EntityKind::FILE, "sample.abc");
    ASSERT_TRUE(structure.Add(std::move(file)));

    EntityNode higher(9U, 1U, EntityKind::METHOD, "Higher");
    higher.primaryRange = FileRange {300U, 10U};
    ASSERT_TRUE(structure.Add(std::move(higher)));

    EntityNode lower(7U, 1U, EntityKind::METHOD, "Lower");
    lower.primaryRange = FileRange {300U, 10U};
    ASSERT_TRUE(structure.Add(std::move(lower)));

    const auto locator = FileRangeLocator::Build(structure, 1024U);
    const auto match = locator->FindMostSpecific(305U);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->entityId, 7U);
    EXPECT_EQ(match->source, FileRangeSource::PRIMARY);
}

TEST(BytecodeExplorerCoreTest, LoadedDocumentRetainsFileRangeLocator)
{
    const auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    EXPECT_NE(loaded.Value().locations, nullptr);
}

}  // namespace panda::bytecode_explorer::test
