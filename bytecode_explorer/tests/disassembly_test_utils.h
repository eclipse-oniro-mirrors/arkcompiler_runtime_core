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

#ifndef BYTECODE_EXPLORER_TESTS_DISASSEMBLY_TEST_UTILS_H
#define BYTECODE_EXPLORER_TESTS_DISASSEMBLY_TEST_UTILS_H

#include "code_data_accessor.h"
#include "core/executable_target_resolver.h"
#include "core/model/entity.h"
#include "core/model/file_range.h"
#include "method_data_accessor-inl.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer::test {

inline EntityNode MakeNode(EntityKind kind, EntityRole role, std::string displayName)
{
    EntityNode node(17U, INVALID_ENTITY_ID, kind, "internal-node-name");
    node.role = role;
    node.displayName = std::move(displayName);
    return node;
}

inline void AddMethod(EntityNode *node, std::string methodName, uint64_t methodOffset,
                      std::optional<FileRange> codeRange)
{
    node->backing.push_back({PhysicalEntityKind::METHOD, FileRange {methodOffset, 8U}, methodName});
    if (codeRange.has_value()) {
        node->backing.push_back({PhysicalEntityKind::CODE, *codeRange, std::move(methodName)});
    }
}

struct BytePatch {
    uint64_t offset {0U};
    std::vector<uint8_t> bytes;
};

class TemporaryPatchedAbc final {
public:
    TemporaryPatchedAbc(const std::string &sourcePath, uint64_t offset, uint8_t value)
        : TemporaryPatchedAbc(sourcePath, std::vector<BytePatch> {{offset, {value}}})
    {
    }

    TemporaryPatchedAbc(const std::string &sourcePath, const std::vector<BytePatch> &patches)
    {
        std::ifstream input(sourcePath, std::ios::binary);
        EXPECT_TRUE(input.is_open());
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        EXPECT_FALSE(bytes.empty());
        for (const auto &patch : patches) {
            EXPECT_LE(patch.offset, bytes.size());
            if (patch.offset > bytes.size() || patch.bytes.size() > bytes.size() - patch.offset) {
                return;
            }
            for (size_t index = 0U; index < patch.bytes.size(); ++index) {
                bytes[patch.offset + index] = static_cast<char>(patch.bytes[index]);
            }
        }

        char path[] = "/tmp/ark_bytecode_explorer_disassembly_XXXXXX";
        const int fd = mkstemp(path);
        EXPECT_NE(fd, -1);
        if (fd == -1) {
            return;
        }
        path_ = path;
        EXPECT_EQ(write(fd, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
        EXPECT_EQ(close(fd), 0);
    }

    ~TemporaryPatchedAbc()
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

inline uint64_t UlebEndOffset(const panda_file::File &file, uint64_t offset)
{
    constexpr size_t maxUleB32Size = 5U;
    for (size_t index = 0U; index < maxUleB32Size && offset < file.GetHeader()->file_size; ++index) {
        if ((file.GetBase()[offset++] & 0x80U) == 0U) {
            return offset;
        }
    }
    ADD_FAILURE() << "Expected a bounded ULEB32 at offset " << offset;
    return file.GetHeader()->file_size;
}

inline std::vector<uint8_t> EncodeUint32(uint32_t value)
{
    return {
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8U),
        static_cast<uint8_t>(value >> 16U),
        static_cast<uint8_t>(value >> 24U),
    };
}

struct CodeLayout {
    uint64_t codeOffset {0U};
    uint64_t codeSizeFieldOffset {0U};
    uint64_t triesSizeFieldOffset {0U};
    uint64_t instructionsOffset {0U};
    uint64_t instructionsEndOffset {0U};
    uint32_t codeSize {0U};
};

CodeLayout GetCodeLayout(const panda_file::File &file, const ExecutableTarget &target);

}  // namespace panda::bytecode_explorer::test

#endif  // BYTECODE_EXPLORER_TESTS_DISASSEMBLY_TEST_UTILS_H
