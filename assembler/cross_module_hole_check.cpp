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

#include "annotation.h"
#include "assembly-function.h"
#include "assembly-record.h"
#include "libpandafile/literal_data_accessor.h"
#include "utils/logger.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace panda::pandasm {

namespace {

// Import graph node: record name -> (external slot -> {source record, local name}).
struct ImportInfo {
    std::string sourceRecord;
    std::string localName;
};
// Per-module import information used by the cross-module hole-check elimination.
// `imports` maps a regular-import ldexternalmodulevar slot to its ImportInfo
// (used to match a throw's slot/name against the importing binding). `sources`
// lists every module this module imports from by ANY edge kind (regular
// import, namespace import, indirect/re-export, star export) and is what cycle
// detection walks. Re-exports (`export { X } from 'mod'`) and star exports
// (`export * from 'mod'`) create import edges too, so they must be included or
// cyclic-via-reexport graphs are misdetected as acyclic and Rule 1 wrongly
// removes throws that guard TDZ bindings (see test262 instn-iee-bndng-*).
struct ModuleNode {
    std::map<uint32_t, ImportInfo> imports;
    std::set<std::string> sources;
};
using ImportGraph = std::map<std::string, ModuleNode>;

// Record name of a module request path/spec. Mirrors the ohmurl conventions:
// @bundle:record / @package:record / @normalized:N&...&record keep their tail
// verbatim; a plain relative path (./b, ../x/b.js) resolves to the file base
// name without extension — the way es2abc names plain-module records.
std::string ModulePathToRecordName(const std::string &path)
{
    constexpr char AND_TOKEN = '&';
    const std::string BUNDLE_PREFIX = "@bundle:";
    const std::string PACKAGE_PREFIX = "@package:";
    const std::string NORMALIZED_PREFIX = "@normalized:N&";
    if (path.rfind(BUNDLE_PREFIX, 0) == 0) {
        return path.substr(BUNDLE_PREFIX.size());
    }
    if (path.rfind(PACKAGE_PREFIX, 0) == 0) {
        return path.substr(PACKAGE_PREFIX.size());
    }
    if (path.rfind(NORMALIZED_PREFIX, 0) == 0) {
        auto andPos = path.find(AND_TOKEN, NORMALIZED_PREFIX.size());
        if (andPos != std::string::npos) {
            return path.substr(andPos + 1);
        }
    }
    auto slashPos = path.find_last_of('/');
    std::string base = (slashPos != std::string::npos) ? path.substr(slashPos + 1) : path;
    auto dotPos = base.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > 0U && dotPos + 1U < base.size()) {
        base = base.substr(0, dotPos);
    }
    return base;
}

// Literal layout produced by the frontend's ModuleRecordEmitter (and restored
// by abc2program), in fixed section order:
//   [INT nReq][STRING spec]*nReq
//   [INT nRegImp][(STRING local, STRING import, METHODAFFILIATE reqIdx)]*nRegImp
//   [INT nNsImp][(STRING local, METHODAFFILIATE reqIdx)]*nNsImp
//   [INT nLocalExp][(STRING local, STRING export)]*nLocalExp
//   [INT nIndExp][(STRING export, STRING import, METHODAFFILIATE reqIdx)]*nIndExp
//   [INT nStarExp][(METHODAFFILIATE reqIdx)]*nStarExp
// Regular imports populate the slot->ImportInfo map (for throw slot/name
// matching). Every edge kind (regular/namespace import, indirect re-export,
// star export) contributes its source record to `sources` so cycle detection
// sees re-export and star cycles, not just regular-import cycles.
// Read the leading INT count of a section and advance the cursor; false on
// truncation or tag mismatch.
bool ReadSectionCount(const LiteralArray &arr, size_t *i, uint32_t *count)
{
    if (*i >= arr.literals_.size() || arr.literals_[*i].tag_ != panda_file::LiteralTag::INTEGER) {
        return false;
    }
    *count = std::get<uint32_t>(arr.literals_[(*i)++].value_);
    return true;
}

// Record the source record of requests[reqIdx] into `sources`.
bool RecordSource(const std::vector<std::string> &requests, std::set<std::string> *sources, uint16_t reqIdx)
{
    if (reqIdx >= requests.size()) {
        return false;
    }
    sources->insert(ModulePathToRecordName(requests[reqIdx]));
    return true;
}

// Every module request is a dependency edge — including side-effect imports
// (`import "./mod"`) which carry a ModuleRequest but no regular-import entry.
// Recording all requests here (not only the ones referenced by import/re-export
// entries) ensures cyclic-via-side-effect-import graphs are seen as cyclic;
// otherwise IsImportAcyclic traverses an empty source set and wrongly returns
// acyclic, and Rule 1 removes a TDZ throw that must fire.
bool ParseRequests(const LiteralArray &arr, size_t *i, std::vector<std::string> *requests,
                   std::set<std::string> *sources)
{
    uint32_t nReq = 0;
    if (!ReadSectionCount(arr, i, &nReq)) {
        return false;
    }
    for (uint32_t r = 0; r < nReq; ++r) {
        if (*i >= arr.literals_.size() || arr.literals_[*i].tag_ != panda_file::LiteralTag::STRING) {
            return false;
        }
        std::string req = std::get<std::string>(arr.literals_[(*i)++].value_);
        sources->insert(ModulePathToRecordName(req));
        requests->push_back(std::move(req));
    }
    return true;
}

// Regular imports: (local, import, reqIdx), emitted in slot order. Fills imports
// and records the source edge.
bool ParseRegularImports(const LiteralArray &arr, size_t *i, const std::vector<std::string> &requests,
                         std::map<uint32_t, ImportInfo> *imports, std::set<std::string> *sources)
{
    uint32_t nReg = 0;
    if (!ReadSectionCount(arr, i, &nReg)) {
        return false;
    }
    for (uint32_t s = 0; s < nReg; ++s) {
        if (*i + 2U >= arr.literals_.size() || arr.literals_[*i].tag_ != panda_file::LiteralTag::STRING ||
            arr.literals_[*i + 1U].tag_ != panda_file::LiteralTag::STRING ||
            arr.literals_[*i + 2U].tag_ != panda_file::LiteralTag::METHODAFFILIATE) {
            return false;
        }
        ImportInfo info;
        info.localName = std::get<std::string>(arr.literals_[*i].value_);
        auto reqIdx = std::get<uint16_t>(arr.literals_[*i + 2U].value_);
        *i += 3U;
        if (!RecordSource(requests, sources, reqIdx)) {
            return false;
        }
        info.sourceRecord = ModulePathToRecordName(requests[reqIdx]);
        (*imports)[s] = info;
    }
    return true;
}

// Namespace imports: (local, reqIdx). Edge to requests[reqIdx].
bool ParseNamespaceImports(const LiteralArray &arr, size_t *i, const std::vector<std::string> &requests,
                           std::set<std::string> *sources)
{
    uint32_t nNs = 0;
    if (!ReadSectionCount(arr, i, &nNs)) {
        return false;
    }
    for (uint32_t s = 0; s < nNs; ++s) {
        if (*i + 1U >= arr.literals_.size() || arr.literals_[*i].tag_ != panda_file::LiteralTag::STRING ||
            arr.literals_[*i + 1U].tag_ != panda_file::LiteralTag::METHODAFFILIATE) {
            return false;
        }
        auto reqIdx = std::get<uint16_t>(arr.literals_[*i + 1U].value_);
        *i += 2U;
        if (!RecordSource(requests, sources, reqIdx)) {
            return false;
        }
    }
    return true;
}

// Local exports: (local, export). No import edge, skip.
bool ParseLocalExports(const LiteralArray &arr, size_t *i)
{
    uint32_t nLocal = 0;
    if (!ReadSectionCount(arr, i, &nLocal)) {
        return false;
    }
    for (uint32_t s = 0; s < nLocal; ++s) {
        if (*i + 1U >= arr.literals_.size() || arr.literals_[*i].tag_ != panda_file::LiteralTag::STRING ||
            arr.literals_[*i + 1U].tag_ != panda_file::LiteralTag::STRING) {
            return false;
        }
        *i += 2U;
    }
    return true;
}

// Indirect exports (re-exports `export { X } from 'mod'`): (export, import, reqIdx).
// These create an import edge to requests[reqIdx] — the edge that was missing
// and let cyclic re-export graphs be misdetected as acyclic.
bool ParseIndirectExports(const LiteralArray &arr, size_t *i, const std::vector<std::string> &requests,
                          std::set<std::string> *sources)
{
    uint32_t nInd = 0;
    if (!ReadSectionCount(arr, i, &nInd)) {
        return false;
    }
    for (uint32_t s = 0; s < nInd; ++s) {
        if (*i + 2U >= arr.literals_.size() || arr.literals_[*i].tag_ != panda_file::LiteralTag::STRING ||
            arr.literals_[*i + 1U].tag_ != panda_file::LiteralTag::STRING ||
            arr.literals_[*i + 2U].tag_ != panda_file::LiteralTag::METHODAFFILIATE) {
            return false;
        }
        auto reqIdx = std::get<uint16_t>(arr.literals_[*i + 2U].value_);
        *i += 3U;
        if (!RecordSource(requests, sources, reqIdx)) {
            return false;
        }
    }
    return true;
}

// Star exports (`export * from 'mod'`): (reqIdx,). Edge to requests[reqIdx].
bool ParseStarExports(const LiteralArray &arr, size_t *i, const std::vector<std::string> &requests,
                      std::set<std::string> *sources)
{
    uint32_t nStar = 0;
    if (!ReadSectionCount(arr, i, &nStar)) {
        return false;
    }
    for (uint32_t s = 0; s < nStar; ++s) {
        if (*i >= arr.literals_.size() || arr.literals_[*i].tag_ != panda_file::LiteralTag::METHODAFFILIATE) {
            return false;
        }
        auto reqIdx = std::get<uint16_t>(arr.literals_[(*i)++].value_);
        if (!RecordSource(requests, sources, reqIdx)) {
            return false;
        }
    }
    return true;
}

// Parse the module-record literal in fixed section order (see header comment):
// regular/namespace imports, local/indirect/star exports. Regular imports fill
// the slot->ImportInfo map (for throw slot/name matching); every edge kind
// contributes its source record to `sources` so cycle detection sees re-export
// and star cycles, not just regular-import cycles.
bool ParseModuleLiteral(const LiteralArray &arr, std::vector<std::string> *requests,
                        std::map<uint32_t, ImportInfo> *imports, std::set<std::string> *sources)
{
    size_t i = 0;
    return ParseRequests(arr, &i, requests, sources) && ParseRegularImports(arr, &i, *requests, imports, sources) &&
           ParseNamespaceImports(arr, &i, *requests, sources) && ParseLocalExports(arr, &i) &&
           ParseIndirectExports(arr, &i, *requests, sources) && ParseStarExports(arr, &i, *requests, sources);
}

// Enumerate (module record name, module literal) pairs of a program. Two
// layouts exist: merge-abc, where the module's own record carries a
// `moduleRecordIdx` static field pointing at the literal; and single-file,
// where every module literal hangs off the shared `_ESModuleRecord` keyed by
// the source file path (module record name = its base name w/o extension).
template <typename Callback>
void ForEachModuleLiteral(const Program &prog, Callback cb)
{
    for (const auto &[recName, record] : prog.record_table) {
        for (const auto &field : record.field_list) {
            std::string moduleName;
            if (field.name == "moduleRecordIdx") {
                moduleName = recName;
            } else if (recName == "_ESModuleRecord" && field.metadata != nullptr) {
                moduleName = ModulePathToRecordName(field.name);
            } else {
                continue;
            }
            if (field.metadata == nullptr) {
                continue;
            }
            auto value = field.metadata->GetValue();
            if (!value.has_value() || value->GetType() != Value::Type::LITERALARRAY) {
                continue;
            }
            auto litIt = prog.literalarray_table.find(value->GetValue<std::string>());
            if (litIt != prog.literalarray_table.end()) {
                cb(moduleName, litIt->second);
            }
        }
    }
}

// Collect import edges from every module record of the program.
void CollectProgramImports(const Program &prog, ImportGraph *graph)
{
    ForEachModuleLiteral(prog, [graph](const std::string &moduleName, const LiteralArray &literal) {
        std::vector<std::string> requests;
        std::map<uint32_t, ImportInfo> imports;
        std::set<std::string> sources;
        if (!ParseModuleLiteral(literal, &requests, &imports, &sources)) {
            return;
        }
        ModuleNode node;
        node.imports = std::move(imports);
        node.sources = std::move(sources);
        (*graph)[moduleName] = std::move(node);
    });
}

// An import is cyclic iff the source record transitively imports back from
// the importing record, by ANY edge kind (regular/namespace import, indirect
// re-export, or star export). Every record on the chain must be present in
// the graph; anything unknown is treated as cyclic (conservative).
bool IsImportAcyclic(const ImportGraph &graph, const std::string &record, const ImportInfo &import)
{
    std::vector<std::string> stack {import.sourceRecord};
    std::set<std::string> visited;
    while (!stack.empty()) {
        auto cur = stack.back();
        stack.pop_back();
        if (cur == record) {
            return false;  // source transitively imports back -> cycle
        }
        if (!visited.insert(cur).second) {
            continue;
        }
        auto it = graph.find(cur);
        if (it == graph.end()) {
            return false;  // record not in this compilation -> cannot rule out a cycle
        }
        for (const auto &src : it->second.sources) {
            stack.push_back(src);
        }
    }
    return true;
}
bool IsExternalLoad(const Ins &ins)
{
    return ins.GetOpcode() == Opcode::LDEXTERNALMODULEVAR || ins.GetOpcode() == Opcode::WIDE_LDEXTERNALMODULEVAR;
}

// A pure acc writer: overwrites the accumulator without reading it, so a
// preceding acc value is dead at this point.
bool OverwritesAccWithoutRead(const Ins &ins)
{
    return !ins.IsLabel() && ins.HasFlag(InstFlags::ACC_WRITE) && !ins.HasFlag(InstFlags::ACC_READ);
}

// The record a function belongs to: the longest module-record name that is a
// name prefix followed by '.' (mangled names keep "record." as the prefix).
// Single-record programs (plain single-file compiles) emit unmangled names;
// `fallback` covers those.
const std::string *OwnerRecord(const std::string &funcName, const ImportGraph &graph, const std::string *fallback)
{
    const std::string *best = nullptr;
    for (const auto &[recName, imports] : graph) {
        (void)imports;
        if (funcName.size() > recName.size() + 1U && funcName.compare(0, recName.size(), recName) == 0 &&
            funcName[recName.size()] == '.') {
            if (best == nullptr || recName.size() > best->size()) {
                best = &recName;
            }
        }
    }
    return (best != nullptr) ? best : fallback;
}

// --- Must-definition dataflow over the linear ins sequence ---
// Like the optimizer's must-availability analysis, but on pandasm ins: track,
// per program point, the unique (must) origin of the accumulator and of each
// register. Origin is either "external import slot N" (the value was produced
// by ldexternalmodulevar N) or unknown. At a merge point the origins must
// agree on every incoming path, otherwise they degrade to unknown.

struct ValueOrigin {
    bool known {false};
    uint32_t slot {0U};

    bool operator==(const ValueOrigin &other) const
    {
        return known == other.known && (!known || slot == other.slot);
    }

    bool operator!=(const ValueOrigin &other) const
    {
        return !(*this == other);
    }
};

const ValueOrigin UNKNOWN_ORIGIN {};

struct FlowState {
    ValueOrigin acc;
    std::unordered_map<uint16_t, ValueOrigin> regs;
    // External slots proven non-hole by a prior throw that executed without
    // firing on every incoming path (must-set; Rule T). Exception edges are
    // not modeled — a catch entry starts from all-unknown — so a gen inside
    // a try region cannot pollute the handler.
    std::unordered_set<uint32_t> proven;

    bool operator==(const FlowState &other) const
    {
        return acc == other.acc && regs == other.regs && proven == other.proven;
    }
};

// Instruction transfer. Only the register-defining instructions of the ES
// subset are modeled (sta/mov); every other acc-writing instruction sets the
// acc origin to unknown, which is the conservative lattice bottom.
void TransferIns(const Ins &ins, FlowState &state)
{
    auto opcode = ins.GetOpcode();
    auto regs = ins.Regs();
    auto imms = ins.Imms();
    switch (opcode) {
        case Opcode::LDEXTERNALMODULEVAR:
        case Opcode::WIDE_LDEXTERNALMODULEVAR:
            if (!imms.empty() && std::holds_alternative<int64_t>(imms[0U])) {
                state.acc = ValueOrigin {true, static_cast<uint32_t>(std::get<int64_t>(imms[0U]))};
            } else {
                state.acc = UNKNOWN_ORIGIN;
            }
            return;
        case Opcode::THROW_UNDEFINEDIFHOLEWITHNAME:
            // Rule T: reaching here means the throw executed without firing
            // (a firing throw never continues), so the checked slot is
            // non-hole from this point on.
            if (state.acc.known) {
                state.proven.insert(state.acc.slot);
            }
            return;
        case Opcode::LDA:
            if (!regs.empty()) {
                auto it = state.regs.find(regs[0U]);
                state.acc = (it != state.regs.end()) ? it->second : UNKNOWN_ORIGIN;
            } else {
                state.acc = UNKNOWN_ORIGIN;
            }
            return;
        case Opcode::STA:
            if (!regs.empty()) {
                state.regs[regs[0U]] = state.acc;  // acc passes through unchanged
            }
            return;
        case Opcode::MOV:
            if (!regs.empty()) {
                state.regs[regs[0U]] = UNKNOWN_ORIGIN;  // parameter copy, not a load
            }
            return;
        default:
            break;
    }
    if (ins.HasFlag(InstFlags::ACC_WRITE)) {
        state.acc = UNKNOWN_ORIGIN;
    }
}

// --- Basic blocks over func.ins (labels are the jump targets) ---

struct BasicBlock {
    size_t begin {0U};  // first non-label ins index
    size_t end {0U};    // one past the last ins (exclusive)
    std::vector<size_t> preds;
    std::vector<size_t> succs;
    bool catchEntry {false};
    bool isEntry {false};  // function entry block; first iteration starts here with no prior throw
    FlowState in;
    FlowState out;
    bool outValid {false};
};

class CfgBuilder {
public:
    explicit CfgBuilder(const Function &func) : func_(func)
    {
        BuildBlocks();
        if (!blocks_.empty()) {
            blocks_[0].isEntry = true;  // block 0 is the function entry (first instruction's block)
        }
        if (!conservative_) {
            BuildEdges();
        }
        if (!conservative_) {
            MarkCatchEntries();
        }
    }

    const std::vector<BasicBlock> &Blocks() const
    {
        return blocks_;
    }

    std::vector<BasicBlock> &Blocks()
    {
        return blocks_;
    }

    // True when the CFG could not be fully resolved (unknown jump target /
    // catch label): the caller must not eliminate anything in this function.
    bool Conservative() const
    {
        return conservative_;
    }

private:
    void BuildBlocks()
    {
        size_t current = NewBlock();
        for (size_t i = 0; i < func_.ins.size(); ++i) {
            const auto &ins = func_.ins[i];
            if (ins->IsLabel()) {
                if (blocks_[current].end > blocks_[current].begin) {
                    current = NewBlock();
                }
                labelToBlock_[ins->Label()] = current;
                continue;
            }
            if (blocks_[current].end == blocks_[current].begin) {
                blocks_[current].begin = i;
            }
            blocks_[current].end = i + 1U;
            if (ins->IsJump() || ins->HasFlag(InstFlags::RETURN)) {
                current = NewBlock();
            }
        }
    }

    void BuildEdges()
    {
        for (size_t b = 0; b < blocks_.size(); ++b) {
            if (blocks_[b].begin == blocks_[b].end) {
                continue;
            }
            const auto &last = func_.ins[blocks_[b].end - 1U];
            bool terminated = last->IsJump() || last->HasFlag(InstFlags::RETURN);
            if (last->IsJump() && !last->Ids().empty()) {
                auto it = labelToBlock_.find(last->Ids()[0U]);
                if (it == labelToBlock_.end()) {
                    conservative_ = true;
                    return;
                }
                AddEdge(b, Resolve(it->second));
            }
            if (!terminated || last->IsConditionalJump()) {
                AddEdge(b, Resolve(b + 1U));
            }
        }
    }

    void MarkCatchEntries()
    {
        for (const auto &cb : func_.catch_blocks) {
            auto it = labelToBlock_.find(cb.catch_begin_label);
            if (it == labelToBlock_.end()) {
                conservative_ = true;
                return;
            }
            auto target = Resolve(it->second);
            if (target < blocks_.size()) {
                blocks_[target].catchEntry = true;
            }
        }
    }

    size_t Resolve(size_t b) const
    {
        while (b < blocks_.size() && blocks_[b].begin == blocks_[b].end) {
            ++b;
        }
        return b;
    }

    size_t NewBlock()
    {
        blocks_.emplace_back();
        return blocks_.size() - 1U;
    }

    void AddEdge(size_t from, size_t to)
    {
        if (to >= blocks_.size()) {
            return;  // fallthrough past the last block
        }
        blocks_[from].succs.push_back(to);
        blocks_[to].preds.push_back(from);
    }

    const Function &func_;
    std::vector<BasicBlock> blocks_;
    std::unordered_map<std::string, size_t> labelToBlock_;
    bool conservative_ {false};
};

// Must-join of predecessor OUT states: fields that agree on every incoming
// path keep their value, everything else degrades to unknown.
FlowState JoinStates(const std::vector<FlowState> &states)
{
    FlowState joined = states[0U];
    for (size_t i = 1U; i < states.size(); ++i) {
        const auto &other = states[i];
        if (joined.acc != other.acc) {
            joined.acc = UNKNOWN_ORIGIN;
        }
        for (auto it = joined.regs.begin(); it != joined.regs.end();) {
            if (other.regs.find(it->first) == other.regs.end() || !(other.regs.at(it->first) == it->second)) {
                it = joined.regs.erase(it);
            } else {
                ++it;
            }
        }
        // Registers known only in `other` are not must-known.
        for (auto it = joined.proven.begin(); it != joined.proven.end();) {
            if (other.proven.find(*it) == other.proven.end()) {
                it = joined.proven.erase(it);
            } else {
                ++it;
            }
        }
    }
    return joined;
}

FlowState ComputeBlockIn(const BasicBlock &bb, const std::vector<BasicBlock> &blocks)
{
    if (bb.catchEntry || bb.preds.empty()) {
        return FlowState {};
    }
    std::vector<FlowState> outs;
    outs.reserve(bb.preds.size() + (bb.isEntry ? 1U : 0U));
    // The entry's first iteration has no prior throw executed; model a fresh-start
    // pred so a back-edge into the entry (e.g. a top-level do-while whose loop
    // head IS the entry) cannot pollute `proven` with a throw that only executed
    // on later iterations — otherwise Rule T removes a TDZ throw that must fire.
    if (bb.isEntry) {
        outs.push_back(FlowState {});
    }
    for (auto p : bb.preds) {
        outs.push_back(blocks[p].out);
    }
    return JoinStates(outs);
}

struct ThrowCheckCtx {
    const std::map<uint32_t, ImportInfo> &imports;
    const ImportGraph &graph;
    const std::string &recordName;
    const FlowState &cur;
};

bool IsThrowRemovable(uint32_t slot, const Ins &ins, const ThrowCheckCtx &ctx)
{
    if (ins.Ids().empty()) {
        return false;
    }
    auto it = ctx.imports.find(slot);
    bool acyclic = it != ctx.imports.end() && it->second.localName == ins.Ids()[0U] &&
                   IsImportAcyclic(ctx.graph, ctx.recordName, it->second);
    return acyclic || ctx.cur.proven.count(slot) != 0U;
}

void SolveDataflow(std::vector<BasicBlock> &blocks, const Function &func)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t b = 0; b < blocks.size(); ++b) {
            auto &bb = blocks[b];
            if (bb.begin == bb.end) {
                continue;
            }
            FlowState in = ComputeBlockIn(bb, blocks);
            FlowState out = in;
            for (size_t i = bb.begin; i < bb.end; ++i) {
                TransferIns(*func.ins[i], out);
            }
            if (!(in == bb.in) || !(out == bb.out) || !bb.outValid) {
                bb.in = std::move(in);
                bb.out = std::move(out);
                bb.outValid = true;
                changed = true;
            }
        }
    }
}

std::vector<size_t> FindRemovableThrows(const std::vector<BasicBlock> &blocks, const Function &func,
                                        const std::string &recordName, const std::map<uint32_t, ImportInfo> &imports,
                                        const ImportGraph &graph)
{
    std::vector<size_t> toRemove;
    for (const auto &bb : blocks) {
        if (bb.begin == bb.end) {
            continue;
        }
        FlowState cur = bb.in;
        for (size_t i = bb.begin; i < bb.end; ++i) {
            const auto &ins = func.ins[i];
            bool isThrow = ins->GetOpcode() == Opcode::THROW_UNDEFINEDIFHOLEWITHNAME && cur.acc.known;
            if (isThrow && IsThrowRemovable(cur.acc.slot, *ins, {imports, graph, recordName, cur})) {
                toRemove.push_back(i);
            }
            TransferIns(*ins, cur);
        }
    }
    return toRemove;
}

void RemoveDeadLoads(Function &func, const std::vector<size_t> &toRemove)
{
    if (toRemove.empty()) {
        return;
    }
    std::vector<bool> removed(func.ins.size(), false);
    for (auto idx : toRemove) {
        removed[idx] = true;
    }
    for (auto idx : toRemove) {
        auto prev = static_cast<std::ptrdiff_t>(idx) - 1;
        while (prev >= 0 && removed[static_cast<size_t>(prev)]) {
            --prev;
        }
        if (prev < 0 || !IsExternalLoad(*func.ins[static_cast<size_t>(prev)])) {
            continue;
        }
        size_t next = idx + 1U;
        while (next < func.ins.size() && removed[next]) {
            ++next;
        }
        if (next >= func.ins.size() || OverwritesAccWithoutRead(*func.ins[next])) {
            removed[static_cast<size_t>(prev)] = true;
            LOG(DEBUG, ASSEMBLER) << "EliminateCrossModuleHoleChecks removes dead external load in " << func.name;
        }
    }
    // Build old-to-new instruction index mapping so that local_variable_debug
    // entries (whose start/length reference instruction positions) stay valid
    // after instructions are erased below.
    const size_t old_size = func.ins.size();
    std::vector<size_t> old_to_new(old_size + 1);
    size_t new_idx = 0;
    for (size_t i = 0; i < old_size; ++i) {
        old_to_new[i] = new_idx;
        if (!removed[i]) {
            ++new_idx;
        }
    }
    old_to_new[old_size] = new_idx;
    for (size_t i = func.ins.size(); i-- > 0U;) {
        if (removed[i]) {
            func.ins.erase(func.ins.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    // Update local_variable_debug to reflect the new instruction indices after
    // erasure. Without this, stale insn_order values in CollectLocalVariable
    // can exceed ins.size() and cause out-of-bounds access in
    // BuildLineNumberProgram (assembly-emitter.cpp).
    for (auto &v : func.local_variable_debug) {
        size_t old_start = std::min(static_cast<size_t>(v.start), old_size);
        size_t old_end = std::min(static_cast<size_t>(v.start) + static_cast<size_t>(v.length), old_size);
        v.start = static_cast<uint32_t>(old_to_new[old_start]);
        v.length = static_cast<uint32_t>(old_to_new[old_end] - old_to_new[old_start]);
    }
}

void EliminateInFunction(Function &func, const std::string &recordName, const std::map<uint32_t, ImportInfo> &imports,
                         const ImportGraph &graph)
{
    CfgBuilder builder(func);
    if (builder.Conservative()) {
        return;
    }
    auto &blocks = builder.Blocks();
    SolveDataflow(blocks, func);
    auto toRemove = FindRemovableThrows(blocks, func, recordName, imports, graph);
    if (toRemove.empty()) {
        return;
    }
    RemoveDeadLoads(func, toRemove);
}

}  // namespace

void EliminateCrossModuleHoleChecks(const std::vector<Program *> &progs)
{
    ImportGraph graph;
    for (auto *prog : progs) {
        CollectProgramImports(*prog, &graph);
    }
    if (graph.empty()) {
        return;
    }
    for (auto *prog : progs) {
        // Single-module programs (plain single-file compiles) emit unmangled
        // function names; those belong to the program's only module record.
        std::string fallbackName;
        size_t ownModules = 0U;
        // clang-format off
        ForEachModuleLiteral(*prog,
            [&fallbackName, &ownModules](const std::string &moduleName, const LiteralArray &literal) {
                (void)literal;
                ++ownModules;
                fallbackName = moduleName;
            });
        // clang-format on
        const std::string *fallback = (ownModules == 1U) ? &fallbackName : nullptr;
        for (auto &[name, func] : prog->function_table) {
            auto *record = OwnerRecord(name, graph, fallback);
            if (record == nullptr) {
                continue;
            }
            auto it = graph.find(*record);
            if (it != graph.end()) {
                EliminateInFunction(func, *record, it->second.imports, graph);
            }
        }
    }
}

}  // namespace panda::pandasm
