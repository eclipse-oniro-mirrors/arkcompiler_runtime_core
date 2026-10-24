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

#include "cross_module_hole_check.h"
#include "assembly-emitter.h"
#include "assembly-ins.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace testing::ext;

namespace panda::pandasm {

namespace {

using Literal = LiteralArray::Literal;

constexpr panda_file::SourceLang LANG = panda_file::SourceLang::ECMASCRIPT;

Literal LitInt(uint32_t v)
{
    return Literal {panda_file::LiteralTag::INTEGER, v};
}
Literal LitStr(std::string s)
{
    return Literal {panda_file::LiteralTag::STRING, std::move(s)};
}
Literal LitAff(uint16_t v)
{
    return Literal {panda_file::LiteralTag::METHODAFFILIATE, v};
}

struct RegularImport {
    std::string local;
    std::string importName;
    uint16_t reqIdx;
};
struct IndirectExport {
    std::string exportName;
    std::string importName;
    uint16_t reqIdx;
};

// Build a module-record literal array in the frontend/abc2program layout:
//   [INT nReq][STRING spec]*nReq
//   [INT nReg][(STRING local, STRING import, METHODAFFILIATE reqIdx)]*nReg
//   [INT nNs][(STRING local, METHODAFFILIATE reqIdx)]*nNs
//   [INT nLocal][(STRING local, STRING export)]*nLocal
//   [INT nInd][(STRING export, STRING import, METHODAFFILIATE reqIdx)]*nInd
//   [INT nStar][(METHODAFFILIATE reqIdx)]*nStar
LiteralArray BuildModuleLiteral(const std::vector<std::string> &requests,
                                const std::vector<RegularImport> &regularImports,
                                const std::vector<IndirectExport> &indirectExports)
{
    std::vector<Literal> lits;
    lits.push_back(LitInt(static_cast<uint32_t>(requests.size())));
    for (const auto &r : requests) {
        lits.push_back(LitStr(r));
    }
    lits.push_back(LitInt(static_cast<uint32_t>(regularImports.size())));
    for (const auto &ri : regularImports) {
        lits.push_back(LitStr(ri.local));
        lits.push_back(LitStr(ri.importName));
        lits.push_back(LitAff(ri.reqIdx));
    }
    lits.push_back(LitInt(0U));  // namespace imports
    lits.push_back(LitInt(0U));  // local exports
    lits.push_back(LitInt(static_cast<uint32_t>(indirectExports.size())));
    for (const auto &ie : indirectExports) {
        lits.push_back(LitStr(ie.exportName));
        lits.push_back(LitStr(ie.importName));
        lits.push_back(LitAff(ie.reqIdx));
    }
    lits.push_back(LitInt(0U));  // star exports
    return LiteralArray(std::move(lits));
}

InsPtr MakeIns(Opcode op, std::vector<IType> imms = {}, std::vector<std::string> ids = {})
{
    std::vector<uint16_t> regs;
    return InsPtr(Ins::CreateIns(op, regs, imms, ids));
}

size_t CountThrows(const Function &func)
{
    size_t c = 0U;
    for (const auto &insp : func.ins) {
        if (insp->OpcodeToString() == "throw.undefinedifholewithname") {
            ++c;
        }
    }
    return c;
}

// Attach a moduleRecordIdx field to a record, pointing at a literal array id.
// This is the merge-abc layout ForEachModuleLiteral reads.
void AttachModuleLiteral(Record &rec, const std::string &litId)
{
    Field f(LANG);
    f.name = "moduleRecordIdx";
    f.metadata->SetValue(ScalarValue::Create<Value::Type::LITERALARRAY>(litId));
    rec.field_list.push_back(std::move(f));
}

Record &AddRecord(Program &prog, const std::string &name)
{
    auto [it, ins] = prog.record_table.insert_or_assign(name, Record(name, LANG));
    (void)ins;
    return it->second;
}

void AddHoleCheckFunction(Program &prog, const std::string &funcKey, const std::string &funcName)
{
    Function func(funcName, LANG);
    func.ins.push_back(MakeIns(Opcode::LDEXTERNALMODULEVAR, {IType(int64_t(0))}));
    func.ins.push_back(MakeIns(Opcode::THROW_UNDEFINEDIFHOLEWITHNAME, {}, {"B"}));
    prog.function_table.insert_or_assign(funcKey, std::move(func));
}

}  // namespace

class CrossModuleHoleCheckTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() {}
    void TearDown() {}
};

/**
 * @tc.name: cyclic_reexport_keeps_throw
 * @tc.desc: Regression for test262 instn-iee-bndng-{cls,const,let}. A regular
 *           import whose source re-exports a binding back from the importer
 *           forms a CYCLE (importer -> fixture -> importer). Re-export
 *           (indirect export) edges were not captured by the import graph, so
 *           the cycle was missed and Rule 1 (acyclic) wrongly removed the TDZ
 *           throw. The throw must be KEPT.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, cyclic_reexport_keeps_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // Module "A" imports B from "FIXTURE" (regular import, slot 0).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./FIXTURE.js"}, {{"B", "B", 0}}, {});
    // Module "FIXTURE" re-exports A as B from A (indirect export) -- cyclic edge.
    prog.literalarray_table["FIX_mod"] = BuildModuleLiteral({"./A.js"}, {}, {{"B", "A", 0}});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "FIXTURE"), "FIX_mod");

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "cyclic re-export must keep the TDZ throw";
}

/**
 * @tc.name: acyclic_import_removes_throw
 * @tc.desc: Positive control: a genuinely acyclic import (source module has no
 *           import/re-export back) lets Rule 1 remove the redundant throw.
 *           Ensures the fix did not over-conservatively disable Rule 1.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, acyclic_import_removes_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // "A" imports B from "SRC"; SRC has no imports/re-exports -> acyclic.
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"B", "B", 0}}, {});
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "acyclic import should let Rule 1 remove the redundant throw";
}

/**
 * @tc.name: star_export_cycle_keeps_throw
 * @tc.desc: A star export (`export * from 'mod'`) also creates an import edge.
 *           A cycle via star export must keep the throw. Verifies star-export
 *           edges are captured by the import graph.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, star_export_cycle_keeps_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // "A" imports B from "SRC" (regular import).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"B", "B", 0}}, {});
    // "SRC" star-exports from A (cycle via star export). Build a literal with a
    // star-export entry: [nReq=1][./A.js][nReg=0][nNs=0][nLocal=0][nInd=0][nStar=1][reqIdx=0].
    std::vector<Literal> starLits;
    starLits.push_back(LitInt(1U));
    starLits.push_back(LitStr("./A.js"));
    starLits.push_back(LitInt(0U));  // nReg
    starLits.push_back(LitInt(0U));  // nNs
    starLits.push_back(LitInt(0U));  // nLocal
    starLits.push_back(LitInt(0U));  // nInd
    starLits.push_back(LitInt(1U));  // nStar
    starLits.push_back(LitAff(0U));  // star export reqIdx 0 -> ./A.js
    prog.literalarray_table["SRC_mod"] = LiteralArray(std::move(starLits));

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U);

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "cyclic star-export must keep the TDZ throw";
}

/**
 * @tc.name: side_effect_import_cycle_keeps_throw
 * @tc.desc: Regression for jokerxd-liu Case 1: a side-effect import
 *           (`import "./mod"`) carries a ModuleRequest but no regular-import
 *           entry. Before the fix ParseModuleLiteral only recorded source edges
 *           from import/re-export ENTRIES, so the side-effect edge was missed
 *           and the a<->b cycle (a imports x from b; b side-effect-imports a)
 *           was misdetected as acyclic -> Rule 1 removed the TDZ throw. Now
 *           every request is recorded as a source edge, so the cycle is seen.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, side_effect_import_cycle_keeps_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports x from FIXTURE (regular import, slot 0).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./FIXTURE.js"}, {{"x", "x", 0}}, {});
    // FIXTURE side-effect-imports A: a request with NO regular-import entry.
    prog.literalarray_table["FIX_mod"] = BuildModuleLiteral({"./A.js"}, {}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "FIXTURE"), "FIX_mod");

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U);

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "side-effect import cycle must keep the TDZ throw";
}

/**
 * @tc.name: entry_loop_head_keeps_throw
 * @tc.desc: Regression for jokerxd-liu Case 2: a top-level do-while compiles
 *           to func_main_0 whose entry block IS the loop head (self-loop via
 *           jeqz back to the entry label). Before the fix ComputeBlockIn joined
 *           only the back-edge OUT (containing the throw's proven={x}) without
 *           the entry's fresh-start, so the fixpoint gave proven={x} at the
 *           throw and Rule T removed it -- but the first iteration has no prior
 *           throw. Now the entry block models a fresh-start pred (FlowState{})
 *           so proven stays {} at the entry, Rule T does not fire, the throw is
 *           kept. Import is cyclic so Rule 1 does not fire either.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, entry_loop_head_keeps_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports x from SRC; SRC imports ready from A (regular) -> cyclic a<->SRC.
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"x", "x", 0}}, {});
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({"./A.js"}, {{"ready", "ready", 0}}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    // A.func_main_0: the entry is the loop head (label L) with a self-loop
    // (jeqz L) -- mirrors a top-level do { console.log(x); } while(cond).
    Function func("A.func_main_0", LANG);
    func.ins.push_back(InsPtr(new LabelIns("L")));
    func.ins.push_back(MakeIns(Opcode::LDEXTERNALMODULEVAR, {IType(int64_t(0))}));
    func.ins.push_back(MakeIns(Opcode::THROW_UNDEFINEDIFHOLEWITHNAME, {}, {"x"}));
    func.ins.push_back(MakeIns(Opcode::JEQZ, {}, {"L"}));
    func.ins.push_back(MakeIns(Opcode::RETURNUNDEFINED));
    prog.function_table.insert_or_assign("A.func_main_0:(any,any,any)", std::move(func));

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U);

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U)
        << "entry-loop-head must keep the TDZ throw (first iteration has no prior throw)";
}

}  // namespace panda::pandasm
