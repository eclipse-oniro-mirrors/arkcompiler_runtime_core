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

#include <gtest/gtest.h>

#include "configs/guard_context.h"
#include "obfuscate/program.h"

using namespace testing::ext;
namespace panda::guard {
class ThisPropertyTest : public testing::Test {
protected:
    void SetUp() override
    {
        const char *argv[] = {"panda_guard", PANDA_GUARD_UNIT_TEST_DIR "configs/this_property_config.json"};
        GuardContext::GetInstance()->Init(2, argv);
    }

    void Check(pandasm::Opcode store, pandasm::Opcode load, int64_t slot, const std::string &name)
    {
        constexpr auto LANG = panda_file::SourceLang::ECMASCRIPT;
        pandasm::Program program;
        pandasm::Record record("probe", LANG);
        pandasm::Field package(LANG);
        package.name = "pkgName@test";
        record.field_list.push_back(std::move(package));
        program.record_table.emplace("probe", std::move(record));
        pandasm::Function function("probe.func_main_0", LANG);
        function.regs_num = 1;
        auto regs = store == pandasm::Opcode::STTHISBYNAME ? std::vector<uint16_t> {} : std::vector<uint16_t> {0};
        function.ins.emplace_back(pandasm::Ins::CreateIns(store, std::move(regs), std::vector<pandasm::IType> {slot},
                                                          std::vector<std::string> {name}));
        function.ins.emplace_back(pandasm::Ins::CreateIns(
            load, std::vector<uint16_t> {}, std::vector<pandasm::IType> {slot}, std::vector<std::string> {name}));
        function.ins.emplace_back(pandasm::Ins::CreateIns(pandasm::Opcode::RETURNUNDEFINED, std::vector<uint16_t> {},
                                                          std::vector<pandasm::IType> {}, std::vector<std::string> {}));
        program.function_table.emplace("probe.func_main_0", std::move(function));
        Program guard(&program);
        guard.Create();
        guard.Obfuscate();
        const auto &ins = program.function_table.at("probe.func_main_0").ins;
        EXPECT_EQ(ins[0]->GetId(0), ins[1]->GetId(0));
        if (name == "kept") {
            EXPECT_EQ(ins[0]->GetId(0), name);
        } else {
            EXPECT_NE(ins[0]->GetId(0), name);
        }
    }
};

HWTEST_F(ThisPropertyTest, old_store_new_load, TestSize.Level1)
{
    Check(pandasm::Opcode::STOBJBYNAME, pandasm::Opcode::LDTHISBYNAME, 0, "secret");
}
HWTEST_F(ThisPropertyTest, new_store_old_load, TestSize.Level1)
{
    Check(pandasm::Opcode::STTHISBYNAME, pandasm::Opcode::LDOBJBYNAME, 0, "secret");
}
HWTEST_F(ThisPropertyTest, both_implicit_this_wide_ic, TestSize.Level1)
{
    Check(pandasm::Opcode::STTHISBYNAME, pandasm::Opcode::LDTHISBYNAME, 256, "secret");
}
HWTEST_F(ThisPropertyTest, reserved_property, TestSize.Level1)
{
    Check(pandasm::Opcode::STTHISBYNAME, pandasm::Opcode::LDTHISBYNAME, 0, "kept");
}
}  // namespace panda::guard
