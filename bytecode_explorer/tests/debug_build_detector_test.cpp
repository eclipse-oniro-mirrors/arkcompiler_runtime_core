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

#include "core/document/abc_document.h"
#include "core/reader/physical_reader.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <unistd.h>

namespace panda::bytecode_explorer::test {
namespace {

// Flips an instruction into an invalid prefixed opcode (legal THROW prefix,
// out-of-range secondary opcode) at the given absolute file offset, the same
// corruption the physical reader tests use to prove malformed input is
// reported instead of aborting the process.
class TemporaryCorruptAbc final {
public:
    TemporaryCorruptAbc(const std::string &sourcePath, uint64_t instructionOffset)
    {
        std::ifstream input(sourcePath, std::ios::binary);
        std::vector<char> bytes {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        EXPECT_GT(bytes.size(), instructionOffset + 1U);
        if (bytes.size() <= instructionOffset + 1U) {
            return;
        }
        bytes[instructionOffset] = static_cast<char>(0xFE);
        bytes[instructionOffset + 1U] = static_cast<char>(0xFF);

        char path[] = "/tmp/ark_bytecode_explorer_build_mode_corrupt_XXXXXX";
        const int fd = mkstemp(path);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return;
        }
        path_ = path;
        EXPECT_EQ(write(fd, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
        EXPECT_EQ(close(fd), 0);
    }

    ~TemporaryCorruptAbc()
    {
        if (!path_.empty()) {
            EXPECT_EQ(unlink(path_.c_str()), 0);
        }
    }

    const std::string &Path() const
    {
        return path_;
    }

private:
    std::string path_;
};

std::optional<uint64_t> FirstMultiByteMethodEvidence(const PhysicalMethod &method)
{
    for (const auto &evidence : method.evidence) {
        if (evidence.range.size >= 2U) {
            return evidence.range.offset;
        }
    }
    return std::nullopt;
}

std::optional<uint64_t> FirstMultiByteInstructionOffset(const PhysicalProgram &program)
{
    for (const auto &record : program.records) {
        for (const auto &method : record.methods) {
            const auto evidence = FirstMultiByteMethodEvidence(method);
            if (evidence.has_value()) {
                return evidence;
            }
        }
    }
    return std::nullopt;
}

TEST(DebugBuildDetectorTest, FlagsDebugBuildFromInstructionAndLineProgramSignals)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_BUILD_MODE_DEBUG_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    const auto &build = document.Value()->Summary().build;

    EXPECT_EQ(build.mode, AbcBuildMode::DEBUG);
    ASSERT_TRUE(build.evidence.scanComplete);
    // The fixture closes over a named local, so a debug compilation must
    // carry the closure's variable-name literal array (debug-only bytecode)
    // and the line programs must describe the named locals.
    EXPECT_GE(build.evidence.debugOnlyInstructions, 1U);
    EXPECT_GE(build.evidence.variableTableEntries, 1U);
    EXPECT_GE(build.evidence.scannedMethods, 2U);
}

TEST(DebugBuildDetectorTest, FlagsReleaseBuildWhenNoDebugOnlySignalsAreFound)
{
    auto document = AbcDocument::Open(BYTECODE_EXPLORER_BUILD_MODE_RELEASE_TEST_ABC);
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    const auto &build = document.Value()->Summary().build;

    EXPECT_EQ(build.mode, AbcBuildMode::RELEASE);
    ASSERT_TRUE(build.evidence.scanComplete);
    EXPECT_EQ(build.evidence.debugOnlyInstructions, 0U);
    EXPECT_EQ(build.evidence.variableTableEntries, 0U);
    EXPECT_EQ(build.evidence.sourceCodeMarkers, 0U);
    EXPECT_GE(build.evidence.scannedMethods, 1U);
}

TEST(DebugBuildDetectorTest, ReportsUnknownWithoutFailingTheOpenWhenBytecodeIsCorrupt)
{
    auto reference = AbcDocument::Open(BYTECODE_EXPLORER_BUILD_MODE_DEBUG_TEST_ABC);
    ASSERT_TRUE(reference.HasValue()) << reference.Error().Message();
    const auto physical = PhysicalReader::Read(reference.Value()->File());
    ASSERT_TRUE(physical.HasValue()) << physical.Error().Message();
    const auto instructionOffset = FirstMultiByteInstructionOffset(physical.Value());
    ASSERT_TRUE(instructionOffset.has_value());

    TemporaryCorruptAbc corrupt(BYTECODE_EXPLORER_BUILD_MODE_DEBUG_TEST_ABC, *instructionOffset);
    ASSERT_FALSE(corrupt.Path().empty());
    auto document = AbcDocument::Open(corrupt.Path());
    ASSERT_TRUE(document.HasValue()) << document.Error().Message();
    const auto &build = document.Value()->Summary().build;

    EXPECT_EQ(build.mode, AbcBuildMode::UNKNOWN);
    EXPECT_FALSE(build.evidence.scanComplete);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
