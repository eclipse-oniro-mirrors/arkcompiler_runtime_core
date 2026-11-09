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
Literal LitInt8(uint8_t v)
{
    return Literal {panda_file::LiteralTag::INTEGER_8, v};
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

/*
 * Build a module-record literal array in the frontend/abc2program layout:
 *   [INT nReq][STRING spec]*nReq
 *   [INT nReg][(STRING local, STRING import, METHODAFFILIATE reqIdx)]*nReg
 *   [INT nNs][(STRING local, METHODAFFILIATE reqIdx)]*nNs
 *   [INT nLocal][(STRING local, STRING export)]*nLocal
 *   [INT nInd][(STRING export, STRING import, METHODAFFILIATE reqIdx)]*nInd
 *   [INT nStar][(METHODAFFILIATE reqIdx)]*nStar
 */
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

/*
 * Build a module-record literal with explicit local exports. Each LocalExport
 * is (localName, exportName). Used to construct the re-export pattern:
 * import { x } from 'mod'; export { x }; — a regular import whose localName
 * matches a local export's localName.
 */
struct LocalExport {
    std::string local;
    std::string exportName;
};
LiteralArray BuildModuleLiteralWithLocalExports(const std::vector<std::string> &requests,
                                                const std::vector<RegularImport> &regularImports,
                                                const std::vector<LocalExport> &localExports)
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
    lits.push_back(LitInt(static_cast<uint32_t>(localExports.size())));
    for (const auto &le : localExports) {
        lits.push_back(LitStr(le.local));
        lits.push_back(LitStr(le.exportName));
    }
    lits.push_back(LitInt(0U));  // indirect exports
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

/*
 * Attach a moduleRequestPhaseIdx field to a record and register the phase
 * literal array in the program. The phase literal has one INTEGER_8 per
 * module request: 0 = non-lazy, 1 = lazy.
 */
void AttachPhaseLiteral(Program &prog, Record &rec, const std::string &phaseLitId, const std::vector<uint8_t> &phases)
{
    std::vector<Literal> lits;
    for (auto p : phases) {
        lits.push_back(LitInt8(p));
    }
    prog.literalarray_table.emplace(phaseLitId, LiteralArray(std::move(lits)));
    Field f(LANG);
    f.name = "moduleRequestPhaseIdx";
    f.metadata->SetValue(ScalarValue::Create<Value::Type::LITERALARRAY>(phaseLitId));
    rec.field_list.push_back(std::move(f));
}

/*
 * Single-file layout: register a module literal under the shared
 * `_ESModuleRecord`, keyed by the source file path (the field name).
 * Get-or-create so multiple module fields can coexist.
 */
void AttachESModuleLiteral(Program &prog, const std::string &sourcePath, const std::string &litId)
{
    auto it = prog.record_table.find("_ESModuleRecord");
    if (it == prog.record_table.end()) {
        it = prog.record_table.emplace("_ESModuleRecord", Record("_ESModuleRecord", LANG)).first;
    }
    Field f(LANG);
    f.name = sourcePath;
    f.metadata->SetValue(ScalarValue::Create<Value::Type::LITERALARRAY>(litId));
    it->second.field_list.push_back(std::move(f));
}

/*
 * Single-file layout: register the phase literal array and hang it off the
 * shared `_ModuleRequestPhaseRecord` under the FIXED field name
 * `moduleRequestPhaseIdx` — NOT keyed by the module path (see
 * Emitter::AddModuleRequestPhaseRecord, non-merge branch).
 */
void AttachSharedPhaseLiteral(Program &prog, const std::string &phaseLitId, const std::vector<uint8_t> &phases)
{
    std::vector<Literal> lits;
    for (auto p : phases) {
        lits.push_back(LitInt8(p));
    }
    prog.literalarray_table.emplace(phaseLitId, LiteralArray(std::move(lits)));
    auto [it, ins] =
        prog.record_table.insert_or_assign("_ModuleRequestPhaseRecord", Record("_ModuleRequestPhaseRecord", LANG));
    (void)ins;
    Field f(LANG);
    f.name = "moduleRequestPhaseIdx";
    f.metadata->SetValue(ScalarValue::Create<Value::Type::LITERALARRAY>(phaseLitId));
    it->second.field_list.push_back(std::move(f));
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

/*
 * Build the standard fixture used by the concurrency test: importers "A",
 * "A1", "A2", ... (importers count) import B from acyclic "SRC"; module
 * "CYC" imports from "FIX" which re-exports back to CYC (cyclic — the throw
 * must be kept on every path).
 */
void BuildConcurrencyFixture(Program &prog, size_t importers)
{
    prog.lang = LANG;
    // SRC must be ATTACHED: an absent non-SDK record is conservatively cyclic.
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");
    prog.literalarray_table["FIX_mod"] = BuildModuleLiteral({"./CYC.js"}, {}, {{"B", "A", 0}});
    for (size_t i = 0U; i < importers; ++i) {
        std::string name = "A" + (i == 0U ? std::string() : std::to_string(i));
        prog.literalarray_table[name + "_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"B", "B", 0}}, {});
        AttachModuleLiteral(AddRecord(prog, name), name + "_mod");
        AddHoleCheckFunction(prog, name + ".func_main_0:(any,any,any)", name + ".func_main_0");
    }
    prog.literalarray_table["CYC_mod"] = BuildModuleLiteral({"./FIX.js"}, {{"B", "B", 0}}, {});
    AttachModuleLiteral(AddRecord(prog, "CYC"), "CYC_mod");
    AttachModuleLiteral(AddRecord(prog, "FIX"), "FIX_mod");
    AddHoleCheckFunction(prog, "CYC.func_main_0:(any,any,any)", "CYC.func_main_0");
}

/**
 * @tc.name: parallel_elimination_matches_serial
 * @tc.desc: With threadCount > 1 the per-function elimination runs on
 *           WorkerQueue worker threads (EmitPrograms passes the emit thread
 *           count). Tasks share no mutable state — each mutates exactly its
 *           own Function and reads the frozen cycle-analysis results — so the
 *           result must be identical to the serial path: acyclic throws
 *           removed, cyclic throws kept, on every function.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, parallel_elimination_matches_serial, TestSize.Level1)
{
    constexpr size_t importers = 16U;  // enough tasks to spread over workers

    Program serialProg;
    BuildConcurrencyFixture(serialProg, importers);
    std::vector<Program *> serialProgs {&serialProg};
    EliminateCrossModuleHoleChecks(serialProgs);  // serial path (default)

    Program parallelProg;
    BuildConcurrencyFixture(parallelProg, importers);
    std::vector<Program *> parallelProgs {&parallelProg};
    EliminateCrossModuleHoleChecks(parallelProgs, 4U);  // worker-queue path

    for (size_t i = 0U; i < importers; ++i) {
        std::string name = "A" + (i == 0U ? std::string() : std::to_string(i));
        auto serialIt = serialProg.function_table.find(name + ".func_main_0:(any,any,any)");
        auto parallelIt = parallelProg.function_table.find(name + ".func_main_0:(any,any,any)");
        ASSERT_NE(serialIt, serialProg.function_table.end());
        ASSERT_NE(parallelIt, parallelProg.function_table.end());
        EXPECT_EQ(CountThrows(serialIt->second), 0U) << name << ": serial must remove the acyclic throw";
        EXPECT_EQ(CountThrows(parallelIt->second), 0U)
            << name << ": parallel path must match serial on the acyclic import";
    }
    auto serialCyc = serialProg.function_table.find("CYC.func_main_0:(any,any,any)");
    auto parallelCyc = parallelProg.function_table.find("CYC.func_main_0:(any,any,any)");
    ASSERT_NE(serialCyc, serialProg.function_table.end());
    ASSERT_NE(parallelCyc, parallelProg.function_table.end());
    EXPECT_EQ(CountThrows(serialCyc->second), 1U) << "serial: cyclic import must keep the throw";
    EXPECT_EQ(CountThrows(parallelCyc->second), 1U) << "parallel: cyclic import must keep the throw";
}

/**
 * @tc.name: malformed_module_literal_disables_fallback
 * @tc.desc: The single-module fallback counts every resolvable module
 *           literal, INCLUDING one whose layout fails to parse. With a
 *           parseable A (SDK import, mangled function) and a malformed B
 *           (truncated literal, unmangled function), the fallback must stay
 *           disabled: B's function is NOT attributed to A and its TDZ throw
 *           stays conservative, while A's own throw is still removed via its
 *           trie attribution.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, malformed_module_literal_disables_fallback, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A: parseable module importing B from an SDK record (acyclic chain).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"@ohos:svc"}, {{"B", "B", 0}}, {});
    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    // B: literal present but truncated (claims 1 request, none follows) —
    // unparseable, so B contributes no import-graph node.
    std::vector<Literal> truncated;
    truncated.push_back(LitInt(1U));  // nReq = 1, then cut off
    prog.literalarray_table["B_mod"] = LiteralArray(std::move(truncated));
    AttachModuleLiteral(AddRecord(prog, "B"), "B_mod");
    AddHoleCheckFunction(prog, "B.func_main_0:(any,any,any)", "B.func_main_0");

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    auto aIt = prog.function_table.find("A.func_main_0:(any,any,any)");
    auto bIt = prog.function_table.find("B.func_main_0:(any,any,any)");
    ASSERT_NE(aIt, prog.function_table.end());
    ASSERT_NE(bIt, prog.function_table.end());
    EXPECT_EQ(CountThrows(aIt->second), 0U) << "A: acyclic SDK import should remove the throw";
    EXPECT_EQ(CountThrows(bIt->second), 1U) << "B: malformed literal must disable the fallback and keep the throw";
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

    EXPECT_EQ(CountThrows(it->second), 1U) << "entry-loop-head must keep the TDZ throw (first iteration)";
}

/**
 * @tc.name: sdk_import_removes_throw
 * @tc.desc: An import from an SDK module (@ohos: prefix) that is NOT present
 *           in the current compilation. SDK modules are pre-compiled system
 *           modules that never import back into project source, so the import
 *           is acyclic even though the SDK record is absent from the graph.
 *           Before the fix, the absent record was conservatively treated as
 *           cyclic and the throw was kept; now it is removed.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, sdk_import_removes_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // "A" imports B from "@ohos:some_sdk_module"; the SDK module is NOT in
    // the compilation (no record, no module literal for it).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"@ohos:some_sdk_module"}, {{"B", "B", 0}}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "SDK import (@ohos:) is acyclic — throw should be removed";
}

/**
 * @tc.name: sdk_prefix_variants_removes_throw
 * @tc.desc: All three SDK prefixes (@ohos:, @system:, @hms) are recognized.
 *           Each import is from a different SDK module prefix, none present in
 *           the compilation; all three throws should be removed.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, sdk_prefix_variants_removes_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // "A" imports from three SDK modules with different prefixes.
    // requests: [0] = @ohos:mod1, [1] = @system:mod2, [2] = @hms:mod3
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"@ohos:mod1", "@system:mod2", "@hms:mod3"},
                                                          {{"B", "B", 0}, {"C", "C", 1U}, {"D", "D", 2U}}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");

    // func_main_0 reads all three slots.
    Function func("A.func_main_0", LANG);
    func.ins.push_back(MakeIns(Opcode::LDEXTERNALMODULEVAR, {IType(int64_t(0))}));
    func.ins.push_back(MakeIns(Opcode::THROW_UNDEFINEDIFHOLEWITHNAME, {}, {"B"}));
    func.ins.push_back(MakeIns(Opcode::LDEXTERNALMODULEVAR, {IType(int64_t(1))}));
    func.ins.push_back(MakeIns(Opcode::THROW_UNDEFINEDIFHOLEWITHNAME, {}, {"C"}));
    func.ins.push_back(MakeIns(Opcode::LDEXTERNALMODULEVAR, {IType(int64_t(2))}));
    func.ins.push_back(MakeIns(Opcode::THROW_UNDEFINEDIFHOLEWITHNAME, {}, {"D"}));
    func.ins.push_back(MakeIns(Opcode::RETURNUNDEFINED));
    prog.function_table.insert_or_assign("A.func_main_0:(any,any,any)", std::move(func));

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 3U) << "fixture: 3 throws present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "all SDK prefixes (@ohos: @system: @hms) are acyclic";
}

/**
 * @tc.name: non_sdk_missing_record_keeps_throw
 * @tc.desc: An import from a non-SDK module that is NOT present in the
 *           compilation. Unlike SDK modules, a missing non-SDK module cannot
 *           be ruled out as cyclic (it might import back from somewhere not
 *           in the compilation), so the throw is kept (conservative).
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, non_sdk_missing_record_keeps_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // "A" imports B from "./missing"; "missing" is NOT in the compilation and
    // is NOT an SDK module (no @ohos: / @system: / @hms prefix).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./missing.js"}, {{"B", "B", 0}}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "non-SDK missing record is conservatively cyclic — throw kept";
}

/**
 * @tc.name: sdk_via_intermediate_removes_throw
 * @tc.desc: An import chain A -> intermediate -> SDK. The intermediate IS in
 *           the compilation and imports from an SDK module (absent). The SDK
 *           record cannot cycle back, so the chain is acyclic and the throw
 *           in A is removed. This verifies the SDK skip works mid-traversal,
 *           not just at the source.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, sdk_via_intermediate_removes_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports B from intermediate (./mid.js). mid imports sdkapi from
    // @ohos:sdk (absent from compilation). A does NOT import from SDK directly.
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./mid.js"}, {{"B", "B", 0}}, {});
    prog.literalarray_table["mid_mod"] = BuildModuleLiteral({"@ohos:sdk"}, {{"sdkapi", "sdkapi", 0}}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "mid"), "mid_mod");

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "chain through SDK is acyclic — throw removed";
}

/**
 * @tc.name: lazy_reexport_keeps_throw
 * @tc.desc: Module B re-exports a binding imported from SRC (import { x } from
 *           SRC; export { x };) AND uses lazy imports (has a
 *           callruntime.ldlazymodulevar instruction). Module A imports x from
 *           B. Even though the import chain A -> B -> SRC is acyclic, B's
 *           re-export of a lazy-imported binding may expose a hole value (the
 *           re-export does not trigger the lazy load). The throw in A must be
 *           KEPT.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, lazy_reexport_keeps_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports B from B (regular import, slot 0).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    /*
     * B imports B from SRC (regular import, slot 0) and re-exports it
     * (local export localName="B"). B also has a lazy load instruction
     * somewhere in its functions.
     */
    prog.literalarray_table["B_mod"] = BuildModuleLiteralWithLocalExports({"./SRC.js"}, {{"B", "B", 0}}, {{"B", "B"}});
    // SRC has no imports — acyclic.
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "B"), "B_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    // B's module request to SRC is lazy: attach a phase literal with [1].
    auto &bRec = prog.record_table.at("B");
    AttachPhaseLiteral(prog, bRec, "B_phase", {1U});

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "lazy re-export module on the chain -> throw kept (binding may be hole)";
}

/**
 * @tc.name: lazy_without_reexport_removes_throw
 * @tc.desc: Module B uses lazy imports (phase literal marks its request as
 *           lazy) but does NOT re-export any imported binding. Module A
 *           imports B from B. Since B does not re-export lazy bindings, the
 *           chain is safe and the throw in A is removed.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, lazy_without_reexport_removes_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports B from B (regular import, slot 0). localName="B" matches the
    // throw's id "B" in AddHoleCheckFunction.
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    // B imports y from SRC (regular import, slot 0). NO local exports (no
    // re-export). B's request to SRC is lazy.
    prog.literalarray_table["B_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"y", "y", 0}}, {});
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "B"), "B_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    // B's module request to SRC is lazy: attach a phase literal with [1].
    auto &bRec = prog.record_table.at("B");
    AttachPhaseLiteral(prog, bRec, "B_phase", {1U});

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "lazy imports without re-export -> acyclic -> throw removed";
}

/**
 * @tc.name: reexport_without_lazy_removes_throw
 * @tc.desc: Module B re-exports a binding from SRC (import { B } from SRC;
 *           export { B };) but does NOT use lazy imports (no phase literal or
 *           phase literal marks request as non-lazy). Module A imports B
 *           from B. The chain is safe and the throw is removed.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, reexport_without_lazy_removes_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports B from B (regular import, slot 0).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    // B imports B from SRC and re-exports it (local export localName="B").
    // NO lazy imports (no phase literal attached).
    prog.literalarray_table["B_mod"] = BuildModuleLiteralWithLocalExports({"./SRC.js"}, {{"B", "B", 0}}, {{"B", "B"}});
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "B"), "B_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    // No phase literal for B -> non-lazy.
    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "re-export without lazy imports -> acyclic -> throw removed";
}

/*
 * === Real frontend implicit-indirect re-export & single-file layout ===
 * The frontend rewrites `import lazy { x } from './SRC.js'; export { x };`
 * into an INDIRECT export that reuses the lazy import's request index
 * (CheckImplicitIndirectExport / ConvertLocalExportToIndirect), and in
 * single-file mode the phase literal hangs off the shared
 * `_ModuleRequestPhaseRecord` under a FIXED field name. These tests cover
 * both layouts.
 */
/**
 * @tc.name: lazy_indirect_reexport_keeps_throw
 * @tc.desc: Real frontend form of `import lazy { B } from './SRC.js';
 *           export { B };`: the local export is converted into an INDIRECT
 *           export (export="B", import="B") that reuses the lazy request
 *           index. The indirect re-export must be checked against the phase
 *           literal, so the throw in A is KEPT.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, lazy_indirect_reexport_keeps_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports B from B (regular import, slot 0).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    /*
     * B: the regular import stays (slot 0), and the re-export is emitted as
     * an INDIRECT export reusing the lazy request 0 — no local exports remain.
     */
    prog.literalarray_table["B_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"B", "B", 0}}, {{"B", "B", 0}});
    // SRC has no imports — acyclic.
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "B"), "B_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    // B's module request to SRC is lazy: attach a phase literal with [1].
    auto &bRec = prog.record_table.at("B");
    AttachPhaseLiteral(prog, bRec, "B_phase", {1U});

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "lazy INDIRECT re-export on the chain -> throw kept";
}

/**
 * @tc.name: indirect_reexport_nonlazy_removes_throw
 * @tc.desc: Explicit `export { B } from './SRC.js';` — an indirect export
 *           whose request the phase literal marks NON-lazy ([0]). Collecting
 *           indirect request indices must not over-block: the chain stays
 *           acyclic and the throw is removed.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, indirect_reexport_nonlazy_removes_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports B from B (regular import, slot 0).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    // B re-exports B from SRC via an explicit INDIRECT export, request 0.
    prog.literalarray_table["B_mod"] = BuildModuleLiteral({"./SRC.js"}, {}, {{"B", "B", 0}});
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "B"), "B_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    // B's request to SRC is NON-lazy: phase literal [0].
    auto &bRec = prog.record_table.at("B");
    AttachPhaseLiteral(prog, bRec, "B_phase", {0U});

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "indirect re-export with NON-lazy request -> acyclic -> throw removed";
}

/**
 * @tc.name: single_file_lazy_indirect_reexport_keeps_throw
 * @tc.desc: Reported scenario in SINGLE-FILE layout: module B re-exports a
 *           lazily imported binding via the implicit indirect form, and the
 *           phase literal hangs off the shared `_ModuleRequestPhaseRecord`
 *           under the fixed field name `moduleRequestPhaseIdx`. The phase
 *           literal must be associated with B (the sole module of its
 *           program), so the throw in A is KEPT. Before the fix the
 *           association used a bogus key, the lookup missed, and the throw
 *           was wrongly removed.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, single_file_lazy_indirect_reexport_keeps_throw, TestSize.Level1)
{
    Program progA;
    progA.lang = LANG;
    Program progB;
    progB.lang = LANG;
    Program progSRC;
    progSRC.lang = LANG;

    // A imports B from B (regular import, slot 0). Single-file layout: the
    // module literal hangs off _ESModuleRecord keyed by the source path.
    progA.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    AttachESModuleLiteral(progA, "./A.js", "A_mod");

    // B: implicit-indirect re-export reusing the lazy request 0.
    progB.literalarray_table["B_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"B", "B", 0}}, {{"B", "B", 0}});
    AttachESModuleLiteral(progB, "./B.js", "B_mod");
    // Single-file phase record: FIXED field name, sole module of the program.
    AttachSharedPhaseLiteral(progB, "B_phase", {1U});

    progSRC.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});
    AttachESModuleLiteral(progSRC, "./SRC.js", "SRC_mod");

    AddHoleCheckFunction(progA, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = progA.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, progA.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&progA, &progB, &progSRC};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "single-file lazy INDIRECT re-export on the chain -> throw kept";
}

/**
 * @tc.name: single_file_phase_nonlazy_removes_throw
 * @tc.desc: Single-file layout with the shared phase record marking B's
 *           request NON-lazy ([0]). The shared phase literal must still be
 *           associated with B, so the re-export is non-lazy, the chain stays
 *           acyclic and the throw is removed.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, single_file_phase_nonlazy_removes_throw, TestSize.Level1)
{
    Program progA;
    progA.lang = LANG;
    Program progB;
    progB.lang = LANG;
    Program progSRC;
    progSRC.lang = LANG;

    progA.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    AttachESModuleLiteral(progA, "./A.js", "A_mod");

    // B re-exports B from SRC via an explicit INDIRECT export, request 0.
    progB.literalarray_table["B_mod"] = BuildModuleLiteral({"./SRC.js"}, {}, {{"B", "B", 0}});
    AttachESModuleLiteral(progB, "./B.js", "B_mod");
    // Single-file phase record: request NON-lazy.
    AttachSharedPhaseLiteral(progB, "B_phase", {0U});

    progSRC.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});
    AttachESModuleLiteral(progSRC, "./SRC.js", "SRC_mod");

    AddHoleCheckFunction(progA, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = progA.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, progA.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&progA, &progB, &progSRC};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "single-file NON-lazy re-export -> acyclic -> throw removed";
}

/**
 * @tc.name: single_file_phase_unresolvable_keeps_throw
 * @tc.desc: Degenerate single-file layout: `_ESModuleRecord` carries two
 *           module fields while the shared `_ModuleRequestPhaseRecord` holds
 *           a single fixed-name phase literal — the association is ambiguous,
 *           so laziness cannot be ruled out. Re-exporting module B
 *           conservatively keeps the throw in A.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, single_file_phase_unresolvable_keeps_throw, TestSize.Level1)
{
    Program progA;
    progA.lang = LANG;
    Program progB;
    progB.lang = LANG;
    Program progSRC;
    progSRC.lang = LANG;

    progA.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    AttachESModuleLiteral(progA, "./A.js", "A_mod");

    // B: implicit-indirect re-export reusing request 0 (lazy in the phase).
    progB.literalarray_table["B_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"B", "B", 0}}, {{"B", "B", 0}});
    // Degenerate layout: TWO module fields on _ESModuleRecord with ONE shared
    // phase record -> the phase literal cannot be associated unambiguously.
    progB.literalarray_table["C_mod"] = BuildModuleLiteral({}, {}, {});
    AttachESModuleLiteral(progB, "./B.js", "B_mod");
    AttachESModuleLiteral(progB, "./C.js", "C_mod");
    AttachSharedPhaseLiteral(progB, "B_phase", {1U});

    progSRC.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});
    AttachESModuleLiteral(progSRC, "./SRC.js", "SRC_mod");

    AddHoleCheckFunction(progA, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = progA.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, progA.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&progA, &progB, &progSRC};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "unresolvable phase association -> laziness undeterminable -> throw kept";
}

/*
 * === Cross-file (multi-Program) scenarios ===
 * EliminateCrossModuleHoleChecks receives std::vector<Program *>. The import
 * graph is built from ALL programs. These tests verify that lazy re-export
 * detection, SDK acyclic, and cycle detection work across program boundaries.
 */
/**
 * @tc.name: cross_file_lazy_reexport_keeps_throw
 * @tc.desc: Module A (in program 1) imports B from module B (in program 2).
 *           Module B re-exports a lazy import (phase literal = 1) from SRC
 *           (in program 3). Even though A and B are in different programs,
 *           the import graph correctly flags B as mightReexportLazy, and the
 *           throw in A is kept.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, cross_file_lazy_reexport_keeps_throw, TestSize.Level1)
{
    Program progA;
    progA.lang = LANG;
    Program progB;
    progB.lang = LANG;
    Program progSRC;
    progSRC.lang = LANG;

    // Program 1: A imports B from B.
    progA.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    AttachModuleLiteral(AddRecord(progA, "A"), "A_mod");

    // Program 2: B imports B from SRC (lazy) and re-exports it.
    progB.literalarray_table["B_mod"] = BuildModuleLiteralWithLocalExports({"./SRC.js"}, {{"B", "B", 0}}, {{"B", "B"}});
    AttachModuleLiteral(AddRecord(progB, "B"), "B_mod");
    auto &bRec = progB.record_table.at("B");
    AttachPhaseLiteral(progB, bRec, "B_phase", {1U});

    // Program 3: SRC has no imports.
    progSRC.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});
    AttachModuleLiteral(AddRecord(progSRC, "SRC"), "SRC_mod");

    AddHoleCheckFunction(progA, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = progA.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, progA.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&progA, &progB, &progSRC};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "cross-file: lazy re-export module on the chain -> throw kept";
}

/**
 * @tc.name: cross_file_sdk_import_removes_throw
 * @tc.desc: Module A (in program 1) imports from @ohos:sdk (not in any
 *           program). The SDK record is absent from all programs. Since
 *           @ohos: prefix is recognized as SDK (never imports back), the
 *           import is acyclic even across program boundaries.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, cross_file_sdk_import_removes_throw, TestSize.Level1)
{
    Program progA;
    progA.lang = LANG;
    Program progB;
    progB.lang = LANG;

    // Program 1: A imports B from @ohos:sdk (absent from all programs).
    progA.literalarray_table["A_mod"] = BuildModuleLiteral({"@ohos:sdk"}, {{"B", "B", 0}}, {});
    AttachModuleLiteral(AddRecord(progA, "A"), "A_mod");

    // Program 2: an unrelated module (not on the import chain).
    progB.literalarray_table["X_mod"] = BuildModuleLiteral({}, {}, {});
    AttachModuleLiteral(AddRecord(progB, "X"), "X_mod");

    AddHoleCheckFunction(progA, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = progA.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, progA.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&progA, &progB};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "cross-file: SDK import acyclic -> throw removed";
}

/**
 * @tc.name: cross_file_cycle_keeps_throw
 * @tc.desc: Module A (in program 1) imports from B (in program 2), and B
 *           imports back from A. The cycle spans two programs. The throw
 *           in A is kept.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, cross_file_cycle_keeps_throw, TestSize.Level1)
{
    Program progA;
    progA.lang = LANG;
    Program progB;
    progB.lang = LANG;

    // Program 1: A imports B from B.
    progA.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    AttachModuleLiteral(AddRecord(progA, "A"), "A_mod");

    // Program 2: B imports back from A — cyclic across programs.
    progB.literalarray_table["B_mod"] = BuildModuleLiteral({"./A.js"}, {{"x", "x", 0}}, {});
    AttachModuleLiteral(AddRecord(progB, "B"), "B_mod");

    AddHoleCheckFunction(progA, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = progA.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, progA.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&progA, &progB};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "cross-file: cyclic import -> throw kept";
}

/**
 * @tc.name: cross_file_acyclic_removes_throw
 * @tc.desc: Module A (in program 1) imports from SRC (in program 2). SRC
 *           has no imports. The import chain is acyclic across two programs.
 *           The throw in A is removed.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, cross_file_acyclic_removes_throw, TestSize.Level1)
{
    Program progA;
    progA.lang = LANG;
    Program progSRC;
    progSRC.lang = LANG;

    // Program 1: A imports B from SRC.
    progA.literalarray_table["A_mod"] = BuildModuleLiteral({"./SRC.js"}, {{"B", "B", 0}}, {});
    AttachModuleLiteral(AddRecord(progA, "A"), "A_mod");

    // Program 2: SRC has no imports.
    progSRC.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});
    AttachModuleLiteral(AddRecord(progSRC, "SRC"), "SRC_mod");

    AddHoleCheckFunction(progA, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = progA.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, progA.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&progA, &progSRC};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 0U) << "cross-file: acyclic import -> throw removed";
}

/**
 * @tc.name: cross_file_lazy_reexport_via_intermediate_keeps_throw
 * @tc.desc: Module A (in program 1) imports from MID (in program 2). MID
 *           imports from B (in program 3, lazy re-export). The lazy
 *           re-export is detected across 3 programs. The throw in A is kept.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, cross_file_lazy_reexport_via_intermediate_keeps_throw, TestSize.Level1)
{
    Program progA;
    progA.lang = LANG;
    Program progMID;
    progMID.lang = LANG;
    Program progB;
    progB.lang = LANG;
    Program progSRC;
    progSRC.lang = LANG;

    // Program 1: A imports B from MID.
    progA.literalarray_table["A_mod"] = BuildModuleLiteral({"./MID.js"}, {{"B", "B", 0}}, {});
    AttachModuleLiteral(AddRecord(progA, "A"), "A_mod");

    // Program 2: MID imports B from B (regular, non-lazy). No re-export.
    progMID.literalarray_table["MID_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    AttachModuleLiteral(AddRecord(progMID, "MID"), "MID_mod");

    // Program 3: B imports B from SRC (lazy) and re-exports it.
    progB.literalarray_table["B_mod"] = BuildModuleLiteralWithLocalExports({"./SRC.js"}, {{"B", "B", 0}}, {{"B", "B"}});
    AttachModuleLiteral(AddRecord(progB, "B"), "B_mod");
    auto &bRec = progB.record_table.at("B");
    AttachPhaseLiteral(progB, bRec, "B_phase", {1U});

    // Program 4: SRC has no imports.
    progSRC.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});
    AttachModuleLiteral(AddRecord(progSRC, "SRC"), "SRC_mod");

    AddHoleCheckFunction(progA, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = progA.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, progA.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&progA, &progMID, &progB, &progSRC};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "cross-file: lazy re-export via intermediate -> throw kept";
}

/**
 * @tc.name: phase_tag_mismatch_keeps_throw
 * @tc.desc: Malformed phase literal — the entry at the re-exported request
 *           index carries an unexpected tag (INTEGER instead of INTEGER_8),
 *           so laziness cannot be determined. Conservatively keep the throw
 *           instead of risking a hole leak (reviewer-requested regression).
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(CrossModuleHoleCheckTest, phase_tag_mismatch_keeps_throw, TestSize.Level1)
{
    Program prog;
    prog.lang = LANG;

    // A imports B from B (regular import, slot 0).
    prog.literalarray_table["A_mod"] = BuildModuleLiteral({"./B.js"}, {{"B", "B", 0}}, {});
    // B re-exports B from SRC (local export form).
    prog.literalarray_table["B_mod"] = BuildModuleLiteralWithLocalExports({"./SRC.js"}, {{"B", "B", 0}}, {{"B", "B"}});
    // SRC has no imports — acyclic.
    prog.literalarray_table["SRC_mod"] = BuildModuleLiteral({}, {}, {});

    AttachModuleLiteral(AddRecord(prog, "A"), "A_mod");
    AttachModuleLiteral(AddRecord(prog, "B"), "B_mod");
    AttachModuleLiteral(AddRecord(prog, "SRC"), "SRC_mod");

    // Malformed phase literal: the entry tag is INTEGER (not INTEGER_8).
    prog.literalarray_table["B_phase"] = LiteralArray({LitInt(1U)});
    Field phaseField(LANG);
    phaseField.name = "moduleRequestPhaseIdx";
    phaseField.metadata->SetValue(ScalarValue::Create<Value::Type::LITERALARRAY>("B_phase"));
    prog.record_table.at("B").field_list.push_back(std::move(phaseField));

    AddHoleCheckFunction(prog, "A.func_main_0:(any,any,any)", "A.func_main_0");

    auto it = prog.function_table.find("A.func_main_0:(any,any,any)");
    ASSERT_NE(it, prog.function_table.end());
    ASSERT_EQ(CountThrows(it->second), 1U) << "fixture: throw present before elimination";

    std::vector<Program *> progs {&prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrows(it->second), 1U) << "phase tag mismatch -> laziness undeterminable -> throw kept";
}

}  // namespace panda::pandasm
