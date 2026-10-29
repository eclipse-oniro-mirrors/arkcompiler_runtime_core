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
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "disassembler.h"
#include <gtest/gtest.h>

using namespace testing::ext;

namespace panda::disasm {

namespace {
std::vector<std::string> SplitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        lines.push_back(line);
    }
    return lines;
}
}  // namespace

class DisassemblerHoleCheckTest : public testing::Test {
public:
    static void SetUpTestCase(void) {};
    static void TearDownTestCase(void) {};
    void SetUp() {};
    void TearDown() {};

    static std::string ReadFile(const std::string &path)
    {
        std::ifstream in(path);
        if (!in.is_open()) {
            return "";
        }
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
};

HWTEST_F(DisassemblerHoleCheckTest, disassembler_hole_check_test_001, TestSize.Level1)
{
    const std::string abcFile = GRAPH_TEST_ABC_DIR "hole-check.abc";
    Disassembler disasm {};
    ASSERT_TRUE(disasm.Disassemble(abcFile, false, false));
    std::ostringstream os;
    disasm.Serialize(os, true, false);
    auto actualLines = SplitLines(os.str());
    auto goldenLines = SplitLines(ReadFile(HOLE_CHECK_GOLDEN_PA));
    ASSERT_FALSE(goldenLines.empty());
    EXPECT_EQ(actualLines.size(), goldenLines.size());
    size_t n = std::min(actualLines.size(), goldenLines.size());
    for (size_t i = 0; i < n; ++i) {
        EXPECT_EQ(actualLines[i], goldenLines[i]) << "line " << (i + 1) << " differs";
    }
}
}  // namespace panda::disasm
