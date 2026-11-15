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

#include "web/api/recent_files.h"

#include <gtest/gtest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace panda::bytecode_explorer::test {
namespace {

class RecentFilesTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        storage_ = "recent_files_test_" + std::to_string(::getpid()) + ".json";
        std::filesystem::remove(storage_);
    }

    void TearDown() override
    {
        std::filesystem::remove(storage_);
        for (const auto &file : created_) {
            std::filesystem::remove(file);
        }
    }

    // The list drops entries whose file is gone, so every recorded path needs a
    // backing file. Content is irrelevant for the recent list.
    std::string CreateFile(const std::string &name)
    {
        std::ofstream output(name, std::ios::binary | std::ios::trunc);
        output << "recent";
        created_.push_back(name);
        return name;
    }

    std::string storage_;
    std::vector<std::string> created_;
};

TEST_F(RecentFilesTest, RecordsEntriesNewestFirstWithBaseNames)
{
    const auto first = CreateFile("recent_first.abc");
    const auto second = CreateFile("recent_second.abc");
    RecentFiles recents(storage_);

    recents.Record(first);
    recents.Record(second);

    const auto entries = recents.List();
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_EQ(entries[0].path, second);
    EXPECT_EQ(entries[0].fileName, "recent_second.abc");
    EXPECT_GT(entries[0].openedAtMs, 0);
    EXPECT_EQ(entries[1].path, first);
}

TEST_F(RecentFilesTest, RerecordingMovesAnEntryToTheFront)
{
    const auto first = CreateFile("recent_first.abc");
    const auto second = CreateFile("recent_second.abc");
    RecentFiles recents(storage_);

    recents.Record(first);
    recents.Record(second);
    recents.Record(first);

    const auto entries = recents.List();
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_EQ(entries[0].path, first);
    EXPECT_EQ(entries[1].path, second);
}

TEST_F(RecentFilesTest, CapsTheListAtEightEntries)
{
    // Recording more paths than RecentFiles::MAX_ENTRIES forces the oldest ones out.
    constexpr int recordedPathCount = 12;
    constexpr int recentFileCap = static_cast<int>(RecentFiles::MAX_ENTRIES);
    std::vector<std::string> paths;
    for (int index = 0; index < recordedPathCount; ++index) {
        paths.push_back(CreateFile("recent_cap_" + std::to_string(index) + ".abc"));
    }
    RecentFiles recents(storage_);

    for (const auto &path : paths) {
        recents.Record(path);
    }

    const auto entries = recents.List();
    ASSERT_EQ(entries.size(), RecentFiles::MAX_ENTRIES);
    // The newest eight of the twelve recordings survive, newest first.
    EXPECT_EQ(entries[0].path, paths[recordedPathCount - 1]);
    EXPECT_EQ(entries[RecentFiles::MAX_ENTRIES - 1U].path, paths[recordedPathCount - recentFileCap]);
}

TEST_F(RecentFilesTest, DropsEntriesWhoseFileDisappeared)
{
    const auto kept = CreateFile("recent_kept.abc");
    const auto gone = CreateFile("recent_gone.abc");
    RecentFiles recents(storage_);

    recents.Record(kept);
    recents.Record(gone);
    std::filesystem::remove(gone);
    created_.erase(std::remove(created_.begin(), created_.end(), gone), created_.end());

    const auto entries = recents.List();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].path, kept);
}

TEST_F(RecentFilesTest, PersistsAcrossInstancesAndIgnoresCorruptedStorage)
{
    const auto path = CreateFile("recent_persist.abc");
    {
        RecentFiles recents(storage_);
        recents.Record(path);
    }
    {
        RecentFiles reloaded(storage_);
        const auto entries = reloaded.List();
        ASSERT_EQ(entries.size(), 1U);
        EXPECT_EQ(entries[0].path, path);
        EXPECT_EQ(entries[0].fileName, "recent_persist.abc");
    }

    {
        std::ofstream output(storage_, std::ios::binary | std::ios::trunc);
        output << "{not json at all";
    }
    RecentFiles corrupted(storage_);
    EXPECT_TRUE(corrupted.List().empty());

    std::filesystem::remove(storage_);
}

TEST_F(RecentFilesTest, EmptyStoragePathKeepsTheListInMemoryOnly)
{
    const auto path = CreateFile("recent_memory.abc");
    RecentFiles recents;

    recents.Record(path);
    EXPECT_EQ(recents.List().size(), 1U);
    EXPECT_FALSE(std::filesystem::exists("recent.json"));
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
