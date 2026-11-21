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

#include "core/reader/hex_window_reader.h"

#include "libpandafile/file.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace panda::bytecode_explorer::test {

TEST(HexWindowReaderTest, ReadsExactBytesFromTheMappedAbc)
{
    const auto opened = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();
    const auto result = HexWindowReader::Read(*opened.Value(), 0U, 32U);
    ASSERT_TRUE(result.HasValue()) << result.Error().Message();
    ASSERT_EQ(result.Value().bytes.size(), 32U);
    EXPECT_TRUE(std::equal(result.Value().bytes.begin(), result.Value().bytes.end(), opened.Value()->File().GetBase()));
    EXPECT_EQ(result.Value().range.offset, 0U);
    EXPECT_EQ(result.Value().range.size, 32U);
    EXPECT_FALSE(result.Value().eof);
}

TEST(HexWindowReaderTest, TruncatesAtEofAndAllowsAnEmptyEofPosition)
{
    const auto opened = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();
    const uint64_t fileSize = opened.Value()->Summary().fileSize;
    const auto tail = HexWindowReader::Read(*opened.Value(), fileSize - 3U, 32U);
    ASSERT_TRUE(tail.HasValue()) << tail.Error().Message();
    EXPECT_EQ(tail.Value().bytes.size(), 3U);
    EXPECT_TRUE(tail.Value().eof);
    const auto eof = HexWindowReader::Read(*opened.Value(), fileSize, 32U);
    ASSERT_TRUE(eof.HasValue()) << eof.Error().Message();
    EXPECT_TRUE(eof.Value().bytes.empty());
    EXPECT_TRUE(eof.Value().eof);
}

TEST(HexWindowReaderTest, RejectsZeroOversizedAndOutOfRangeRequests)
{
    const auto opened = AbcDocument::Open(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(opened.HasValue()) << opened.Error().Message();
    EXPECT_FALSE(HexWindowReader::Read(*opened.Value(), 0U, 0U).HasValue());
    EXPECT_FALSE(HexWindowReader::Read(*opened.Value(), 0U, HexWindowReader::MAX_WINDOW_SIZE + 1U).HasValue());
    EXPECT_FALSE(HexWindowReader::Read(*opened.Value(), opened.Value()->Summary().fileSize + 1U, 1U).HasValue());
    EXPECT_FALSE(HexWindowReader::Read(*opened.Value(), std::numeric_limits<uint64_t>::max(), 1U).HasValue());
}

}  // namespace panda::bytecode_explorer::test
