/**
 * Copyright (c) 2023-2026 Huawei Device Co., Ltd.
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

#include "verifier.h"
#include "utils.h"

#include <gtest/gtest.h>
#include <string>
#include <cstdlib>
#include <fstream>

#include "file.h"
#include "utils/logger.h"
#include "class_data_accessor-inl.h"
#include "code_data_accessor-inl.h"
#include "method_data_accessor-inl.h"

using namespace testing::ext;

namespace panda::verifier {
class VerifierRegisterTest : public testing::Test {
public:
    static void SetUpTestCase(void) {};
    static void TearDownTestCase(void) {};
    void SetUp() {};
    void TearDown() {};
};

/**
* @tc.name: verifier_test_001
* @tc.desc: Verify the abc file register index function.
* @tc.type: FUNC
* @tc.require: file path and name
*/
HWTEST_F(VerifierRegisterTest, verifier_register_001, TestSize.Level1)
{
    const std::string file_name = GRAPH_TEST_ABC_DIR "test_register_index.abc";
    panda::verifier::Verifier ver {file_name};
    ver.CollectIdInfos();
    EXPECT_TRUE(ver.VerifyRegisterIndex());
    EXPECT_TRUE(ver.Verify());
}

/**
* @tc.name: verifier_test_002
* @tc.desc: Verify the modified abc file register index function.
* @tc.type: FUNC
* @tc.require: file path and name
*/
HWTEST_F(VerifierRegisterTest, verifier_register_002, TestSize.Level1)
{
    const std::string base_file_name = GRAPH_TEST_ABC_DIR "test_register_index.abc";
    {
        panda::verifier::Verifier ver {base_file_name};
        ver.CollectIdInfos();
        EXPECT_TRUE(ver.VerifyRegisterIndex());
    }
    // the new register index in the abc file
    const uint8_t new_reg_id = 0x09;

    std::ifstream base_file(base_file_name, std::ios::binary);
    if (!base_file.is_open()) {
        LOG(ERROR, VERIFIER) << "Failed to open file " << base_file_name;
        EXPECT_TRUE(base_file.is_open());
    }
    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(base_file), {});
    // the known instruction which contains register index in the abc file
    const std::vector<uint8_t> op_code = {0x60, 0x03};

    for (size_t i = 0; i < buffer.size() - 2; i++) {
        if (buffer[i] == op_code[0] && buffer[i+1] == op_code[1]) {
            buffer[i + 1] = static_cast<unsigned char>(new_reg_id);
        }
    }

    const std::string tar_file_name = GRAPH_TEST_ABC_DIR "verifier_register_002.abc";
    GenerateModifiedAbc(buffer, tar_file_name);
    base_file.close();

    {
        panda::verifier::Verifier ver {tar_file_name};
        ver.CollectIdInfos();
        EXPECT_FALSE(ver.VerifyRegisterIndex());
    }
}

/**
* @tc.name: verifier_test_003
* @tc.desc: Verify the range register index of the abc file.
* @tc.type: FUNC
* @tc.require: file path and name
*/
HWTEST_F(VerifierRegisterTest, verifier_register_003, TestSize.Level1)
{
    const std::string base_file_name = GRAPH_TEST_ABC_DIR "test_register_index.abc";
    panda::verifier::Verifier original {base_file_name};
    original.CollectIdInfos();
    ASSERT_TRUE(original.VerifyRegisterIndex());
    std::ifstream base_file(base_file_name, std::ios::binary);
    ASSERT_TRUE(base_file.is_open());
    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(base_file), {});

    auto file = panda_file::File::Open(base_file_name);
    ASSERT_NE(file, nullptr);
    std::vector<panda_file::File::EntityId> method_ids;
    for (auto class_id : file->GetClasses()) {
        panda_file::File::EntityId id {class_id};
        if (!file->IsExternal(id)) {
            panda_file::ClassDataAccessor klass {*file, id};
            klass.EnumerateMethods(
                [&](panda_file::MethodDataAccessor &method) { method_ids.push_back(method.GetMethodId()); });
        }
    }
    bool modified = false;
    for (auto method_id : method_ids) {
        panda_file::MethodDataAccessor method {*file, method_id};
        if (!method.GetCodeId().has_value()) {
            continue;
        }
        panda_file::CodeDataAccessor code {*file, method.GetCodeId().value()};
        auto inst = BytecodeInstruction(code.GetInstructions());
        const auto *end = code.GetInstructions() + code.GetCodeSize();
        while (inst.GetAddress() < end) {
            if (inst.GetOpcode() == Opcode::NEWOBJRANGE_IMM8_IMM8_V8) {
                constexpr uint8_t INVALID_RANGE_SIZE = 0xff;
                ASSERT_LT(code.GetNumVregs() + code.GetNumArgs(), INVALID_RANGE_SIZE);
                // The count follows the opcode and IC slot in this instruction format.
                auto offset = static_cast<size_t>(inst.GetAddress() - file->GetBase()) + 2U;
                ASSERT_NE(buffer.at(offset), INVALID_RANGE_SIZE);
                buffer.at(offset) = INVALID_RANGE_SIZE;
                modified = true;
                break;
            }
            inst = inst.GetNext();
        }
        if (modified) {
            break;
        }
    }
    ASSERT_TRUE(modified);

    const std::string tar_file_name = GRAPH_TEST_ABC_DIR "verifier_register_003.abc";
    GenerateModifiedAbc(buffer, tar_file_name);
    panda::verifier::Verifier ver {tar_file_name};
    ver.CollectIdInfos();
    EXPECT_FALSE(ver.VerifyRegisterIndex());
}

} // namespace panda::verifier
