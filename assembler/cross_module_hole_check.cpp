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
#include "libpandabase/utils/workerQueue.h"
#include "utils/logger.h"

#include <algorithm>
#include <cstdint>
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
    uint16_t reqIdx {0};
    /*
     * Cached verdict of the one-shot import-cycle analysis
     * (AnalyzeImportCycles): true when this import is provably acyclic (and
     * not polluted by a lazy re-export on the chain), so the throw guarding
     * its slot is removable without any per-throw graph walk. Defaults to
     * false (cyclic — keep the throw) so every import the analysis did not
     * explicitly clear stays conservative.
     */
    bool acyclic {false};
};
/*
 * Per-module import information used by the cross-module hole-check elimination.
 * `imports` maps a regular-import ldexternalmodulevar slot to its ImportInfo
 * (used to match a throw's slot/name against the importing binding). `sources`
 * lists every module this module imports from by ANY edge kind (regular
 * import, namespace import, indirect/re-export, star export) and is what cycle
 * detection walks. Re-exports (`export { X } from 'mod'`) and star exports
 * (`export * from 'mod'`) create import edges too, so they must be included or
 * cyclic-via-reexport graphs are misdetected as acyclic and Rule 1 wrongly
 * removes throws that guard TDZ bindings (see test262 instn-iee-bndng-*).
 */
struct ModuleNode {
    std::map<uint32_t, ImportInfo> imports;
    std::set<std::string> sources;
    /*
     * True when the module re-exports a binding whose source module request is
     * a lazy import (recorded in the module-request phase literal buffer as
     * INTEGER_8 = 1), so the re-export does not trigger the lazy load and the
     * binding may still be hole even in an acyclic chain. Covers bindings only
     * re-exported (never read, so no ldlazymodulevar is generated) which
     * bytecode scanning would miss. Two literal forms carry re-exports:
     *  - local re-export: a regular import whose localName matches a local
     *    export (`import { x } from 'mod'; export { x };`);
     *  - indirect re-export: `export { x } from 'mod'`, and — importantly —
     *    the implicit indirect form the frontend generates from the ArkTS
     *    lazy pattern `import lazy { x } from 'mod'; export { x };`
     *    (CheckImplicitIndirectExport / ConvertLocalExportToIndirect moves the
     *    local export into the indirect section and REUSES the lazy import's
     *    request index).
     * Star exports (`export * from 'mod'`) only ever create their own non-lazy
     * request, so they carry no re-export request index. ModuleRequestRecord
     * deduplicates by (source, isLazy), so an explicit non-lazy re-export of a
     * lazily imported path references the non-lazy request — the phase literal
     * stays the single source of truth for laziness.
     */
    bool mightReexportLazy {false};
};
using ImportGraph = std::map<std::string, ModuleNode>;

/*
 * Record name of a module request path/spec. Mirrors the ohmurl conventions:
 * @bundle:record / @package:record / @normalized:N&...&record keep their tail
 * verbatim; a plain relative path (./b, ../x/b.js) resolves to the file base
 * name without extension — the way es2abc names plain-module records.
 */
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

/*
 * Literal layout produced by the frontend's ModuleRecordEmitter (and restored
 * by abc2program), in fixed section order:
 *   [INT nReq][STRING spec]*nReq
 *   [INT nRegImp][(STRING local, STRING import, METHODAFFILIATE reqIdx)]*nRegImp
 *   [INT nNsImp][(STRING local, METHODAFFILIATE reqIdx)]*nNsImp
 *   [INT nLocalExp][(STRING local, STRING export)]*nLocalExp
 *   [INT nIndExp][(STRING export, STRING import, METHODAFFILIATE reqIdx)]*nIndExp
 *   [INT nStarExp][(METHODAFFILIATE reqIdx)]*nStarExp
 * Regular imports populate the slot->ImportInfo map (for throw slot/name
 * matching). Every edge kind (regular/namespace import, indirect re-export,
 * star export) contributes its source record to `sources` so cycle detection
 * sees re-export and star cycles, not just regular-import cycles.
 * Read the leading INT count of a section and advance the cursor; false on
 * truncation or tag mismatch.
 */
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

/*
 * Every module request is a dependency edge — including side-effect imports
 * (`import "./mod"`) which carry a ModuleRequest but no regular-import entry.
 * Recording all requests here (not only the ones referenced by import/re-export
 * entries) ensures cyclic-via-side-effect-import graphs are seen as cyclic;
 * otherwise the cycle analysis traverses an empty source set and wrongly
 * returns acyclic, and Rule 1 removes a TDZ throw that must fire.
 */
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
        info.reqIdx = reqIdx;
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

/*
 * Local exports: (local, export). No import edge, but record the local names
 * so re-exports (import { x } from 'mod'; export { x };) can be detected by
 * matching a regular import's localName against this set.
 */
bool ParseLocalExports(const LiteralArray &arr, size_t *i, std::set<std::string> *localExportNames)
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
        if (localExportNames != nullptr) {
            localExportNames->insert(std::get<std::string>(arr.literals_[*i].value_));
        }
        *i += 2U;
    }
    return true;
}

/*
 * Indirect exports (re-exports `export { X } from 'mod'`): (export, import, reqIdx).
 * These create an import edge to requests[reqIdx] — the edge that was missing
 * and let cyclic re-export graphs be misdetected as acyclic. The request index
 * is also collected in `reexportReqIdxs` for lazy-re-export detection: the
 * frontend rewrites `import lazy { x } from 'mod'; export { x };` into an
 * INDIRECT export that reuses the lazy import's request index
 * (CheckImplicitIndirectExport / ConvertLocalExportToIndirect), so a lazy
 * request can hide here. Explicit `export { x } from 'mod'` requests are
 * always non-lazy; checking them against the phase literal is a no-op.
 */
bool ParseIndirectExports(const LiteralArray &arr, size_t *i, const std::vector<std::string> &requests,
                          std::set<std::string> *sources, std::set<uint16_t> *reexportReqIdxs)
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
        if (reexportReqIdxs != nullptr) {
            reexportReqIdxs->insert(reqIdx);
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

/*
 * Parse the module-record literal in fixed section order (see header comment):
 * regular/namespace imports, local/indirect/star exports. Regular imports fill
 * the slot->ImportInfo map (for throw slot/name matching); every edge kind
 * contributes its source record to `sources` so cycle detection sees re-export
 * and star cycles, not just regular-import cycles. `reexportReqIdxs` collects
 * the module-request indices of re-exports so the caller can check them
 * against the phase literal for lazy detection:
 *  - indirect re-exports (collected in ParseIndirectExports): the frontend
 *    rewrites the ArkTS lazy pattern `import lazy { x } from 'mod';
 *    export { x };` into an INDIRECT export that REUSES the lazy import's
 *    request index (CheckImplicitIndirectExport / ConvertLocalExportToIndirect),
 *    so a lazy request can hide there;
 *  - local re-exports (matched below): a regular import whose localName equals
 *    a local export's localName — hand-written pandasm may still use this
 *    form even though the frontend emits the implicit-indirect form above.
 * Star exports (`export * from 'mod'`) only ever create their own non-lazy
 * request, so they contribute no re-export request index.
 */
bool ParseModuleLiteral(const LiteralArray &arr, std::vector<std::string> *requests,
                        std::map<uint32_t, ImportInfo> *imports, std::set<std::string> *sources,
                        std::set<uint16_t> *reexportReqIdxs)
{
    size_t i = 0;
    std::set<std::string> localExportNames;
    bool ok = ParseRequests(arr, &i, requests, sources) && ParseRegularImports(arr, &i, *requests, imports, sources) &&
              ParseNamespaceImports(arr, &i, *requests, sources) && ParseLocalExports(arr, &i, &localExportNames) &&
              ParseIndirectExports(arr, &i, *requests, sources, reexportReqIdxs) &&
              ParseStarExports(arr, &i, *requests, sources);
    // Local re-exports: a regular import whose localName matches a local
    // export. Add the import's reqIdx so the caller can check laziness.
    if (ok && reexportReqIdxs != nullptr) {
        for (const auto &[slot, info] : *imports) {
            (void)slot;
            if (localExportNames.count(info.localName) != 0U) {
                reexportReqIdxs->insert(info.reqIdx);
            }
        }
    }
    return ok;
}

// Resolve the module name for a moduleRecordIdx field, or empty if not a module literal field.
static std::string ResolveModuleName(const std::string &recName, const pandasm::Field &field)
{
    if (field.name == "moduleRecordIdx") {
        return recName;
    }
    if (recName == "_ESModuleRecord" && field.metadata != nullptr) {
        return ModulePathToRecordName(field.name);
    }
    return {};
}

// Phase literal-array ids keyed by module name, plus the ids collected from
// the shared _ModuleRequestPhaseRecord (single-file layout, not module-keyed).
struct PhaseLiteralIds {
    std::unordered_map<std::string, std::string> perModule;
    std::vector<std::string> shared;
};

/*
 * Extract the phase literal-array id from a `moduleRequestPhaseIdx` field, or
 * empty when the field is not a literal-array valued phase field.
 */
static std::string PhaseLiteralIdOf(const pandasm::Field &field)
{
    if (field.name != "moduleRequestPhaseIdx" || field.metadata == nullptr) {
        return {};
    }
    auto value = field.metadata->GetValue();
    if (!value.has_value() || value->GetType() != Value::Type::LITERALARRAY) {
        return {};
    }
    return value->GetValue<std::string>();
}

// Fill the per-record and shared phase literal ids of every record in the program.
static void CollectPhaseIdPerRecord(const Program &prog, PhaseLiteralIds &out)
{
    for (const auto &[recName, record] : prog.record_table) {
        for (const auto &field : record.field_list) {
            auto id = PhaseLiteralIdOf(field);
            if (id.empty()) {
                continue;
            }
            if (recName == "_ModuleRequestPhaseRecord") {
                out.shared.push_back(std::move(id));
            } else {
                out.perModule[recName] = std::move(id);
            }
        }
    }
}

// Collect the module names of every module literal field in the program.
static std::set<std::string> CollectModuleNames(const Program &prog)
{
    std::set<std::string> moduleNames;
    for (const auto &[recName, record] : prog.record_table) {
        for (const auto &field : record.field_list) {
            auto name = ResolveModuleName(recName, field);
            if (!name.empty()) {
                moduleNames.insert(std::move(name));
            }
        }
    }
    return moduleNames;
}

/*
 * Collect the phase literal-array ids of a program. The phase literal contains
 * one INTEGER_8 per module request in the same order as the requests section:
 * 0 = non-lazy, 1 = lazy. Two layouts exist (see Emitter::AddModuleRequestPhaseRecord):
 *  - merge-abc: each module's own record carries a `moduleRequestPhaseIdx`
 *    static field pointing at its phase literal — keyed by the record name;
 *  - single-file: module literals hang off the shared `_ESModuleRecord` keyed
 *    by the source file path, while the shared `_ModuleRequestPhaseRecord`
 *    carries a FIXED field name (`moduleRequestPhaseIdx`) that is NOT a module
 *    key. The layout has exactly one module record (the compiled file itself),
 *    so associate the shared phase literal with the sole module when
 *    unambiguous; otherwise leave it unresolved — the caller then treats every
 *    re-exporting module as potentially lazy (keeps throws, see
 *    CheckLazyReexport).
 */
static PhaseLiteralIds CollectPhaseLiteralIds(const Program &prog)
{
    PhaseLiteralIds out;
    CollectPhaseIdPerRecord(prog, out);
    if (out.shared.empty()) {
        return out;
    }
    auto moduleNames = CollectModuleNames(prog);
    if (out.shared.size() == 1 && moduleNames.size() == 1) {
        out.perModule[*moduleNames.begin()] = out.shared.front();
    }
    return out;
}

// Look up the phase literal for a module, or nullptr if not present.
static const LiteralArray *LookupPhaseLiteral(const Program &prog, const std::string &moduleName,
                                              const std::unordered_map<std::string, std::string> &phaseLiteralIds)
{
    auto phaseIt = phaseLiteralIds.find(moduleName);
    if (phaseIt == phaseLiteralIds.end()) {
        return nullptr;
    }
    auto pLitIt = prog.literalarray_table.find(phaseIt->second);
    return (pLitIt != prog.literalarray_table.end()) ? &pLitIt->second : nullptr;
}

template <typename Callback>
void ForEachModuleLiteral(const Program &prog, Callback cb)
{
    auto phaseLiteralIds = CollectPhaseLiteralIds(prog);
    for (const auto &[recName, record] : prog.record_table) {
        for (const auto &field : record.field_list) {
            auto moduleName = ResolveModuleName(recName, field);
            if (moduleName.empty() || field.metadata == nullptr) {
                continue;
            }
            auto value = field.metadata->GetValue();
            if (!value.has_value() || value->GetType() != Value::Type::LITERALARRAY) {
                continue;
            }
            auto litIt = prog.literalarray_table.find(value->GetValue<std::string>());
            if (litIt == prog.literalarray_table.end()) {
                continue;
            }
            cb(moduleName, litIt->second, LookupPhaseLiteral(prog, moduleName, phaseLiteralIds.perModule));
        }
    }
}

/*
 * Check if any re-export (local or indirect) references a lazy module request.
 * Returns true if a lazy re-export is found or if laziness cannot be ruled out
 * (malformed phase entry: out-of-range index or unexpected tag — treating
 * those as lazy keeps the throw instead of risking a hole leak). When the
 * phase literal is missing but the program carries a shared
 * `_ModuleRequestPhaseRecord` (single-file layout whose phase literal could not
 * be associated with the module), laziness is undeterminable and any re-export
 * conservatively keeps the throw. Without that record no request in the program
 * is lazy at all, so re-exports are safely non-lazy.
 */
static bool CheckLazyReexport(const std::set<uint16_t> &reexportReqIdxs, const LiteralArray *phaseLiteral,
                              bool hasSharedPhaseRecord)
{
    if (phaseLiteral == nullptr) {
        return hasSharedPhaseRecord && !reexportReqIdxs.empty();
    }
    for (auto reqIdx : reexportReqIdxs) {
        if (reqIdx >= phaseLiteral->literals_.size()) {
            return true;  // malformed: cannot rule out lazy
        }
        if (phaseLiteral->literals_[reqIdx].tag_ != panda_file::LiteralTag::INTEGER_8) {
            return true;  // malformed: unexpected tag, cannot rule out lazy
        }
        if (std::get<uint8_t>(phaseLiteral->literals_[reqIdx].value_) != 0U) {
            return true;
        }
    }
    return false;
}

/*
 * Module names resolved in one program while collecting imports. Used for the
 * single-module fallback attribution: when exactly one module literal
 * resolves, unmangled function names belong to that record (see
 * EliminateCrossModuleHoleChecks). Collected during the import scan so the
 * caller does not re-enumerate every record field a second time.
 */
struct ProgramModuleNames {
    size_t count {0U};
    std::string last;
};

// Collect import edges from every module record of the program. The phase
// literal (if present) records which module requests are lazy imports.
void CollectProgramImports(const Program &prog, ImportGraph *graph, ProgramModuleNames *moduleNames)
{
    bool hasSharedPhaseRecord = prog.record_table.find("_ModuleRequestPhaseRecord") != prog.record_table.end();
    ForEachModuleLiteral(prog, [&graph, moduleNames, hasSharedPhaseRecord](const std::string &moduleName,
                                                                           const LiteralArray &literal,
                                                                           const LiteralArray *phaseLiteral) {
        /*
         * Count `moduleName` BEFORE parsing: the single-module fallback counts
         * every resolvable module literal, including ones whose layout fails
         * to parse. A program with one parseable and one malformed module
         * must NOT attribute its unmangled functions to the parseable module
         * — the malformed module's unrecognized layout has to stay
         * conservative (fallback disabled, throws kept).
         */
        ++moduleNames->count;
        moduleNames->last = moduleName;
        std::vector<std::string> requests;
        std::map<uint32_t, ImportInfo> imports;
        std::set<std::string> sources;
        std::set<uint16_t> reexportReqIdxs;
        if (!ParseModuleLiteral(literal, &requests, &imports, &sources, &reexportReqIdxs)) {
            return;
        }
        ModuleNode node;
        node.imports = std::move(imports);
        node.sources = std::move(sources);
        node.mightReexportLazy = CheckLazyReexport(reexportReqIdxs, phaseLiteral, hasSharedPhaseRecord);
        (*graph)[moduleName] = std::move(node);
    });
}

/*
 * SDK module record names start with these prefixes. SDK modules are
 * pre-compiled and shipped with the system; they never import back into
 * project source, so an import chain that reaches an SDK record cannot be
 * cyclic — even when the SDK record is absent from the current compilation
 * (the normal case: the SDK is not part of the project's merge-abc input).
 */
bool IsSdkRecord(const std::string &recordName)
{
    return recordName.rfind("@ohos:", 0) == 0 || recordName.rfind("@system:", 0) == 0 ||
           recordName.rfind("@hms:", 0) == 0;
}

/*
 * An import record -> source is cyclic iff the source record transitively
 * imports back to the importing record, by ANY edge kind (regular/namespace
 * import, indirect re-export, or star export); iff any module on the chain
 * might re-export a lazy-imported binding (a lazy re-export does not trigger
 * module loading, so the binding may still be hole even in an acyclic chain);
 * or iff any record on the chain is absent from the graph (unknown is
 * conservative) — EXCEPT SDK records (@ohos: / @system: / @hms), which are
 * pre-compiled system modules that never import back into project source.
 *
 * AnalyzeImportCycles answers those three questions for the WHOLE graph in
 * one O(V + E) pass and caches the verdict per import edge, instead of
 * re-running a graph DFS for every throw (the previous IsImportAcyclic, which
 * cost O(#throws * (V + E)) with log-factor set/map operations):
 *   * "source reaches back to record": every parsed import adds the edge
 *     record -> source to the importer's `sources`, so this holds iff record
 *     and source are in the same strongly connected component (Tarjan);
 *   * "chain contains a lazy-reexport module" and "chain reaches an absent
 *     non-SDK record": pure forward-reachability properties of the source
 *     node, precomputed by one backward propagation over reverse edges each;
 *   * a source itself absent from the compilation is resolved at cache-fill
 *     time (SDK -> acyclic, anything else -> conservative cyclic).
 */
/*
 * Id-indexed view of the import graph used by the cycle analysis: forward and
 * reverse adjacency over in-graph nodes, plus two per-node cyclic-verdict
 * seeds — `reachesUnknownSource` marks a node importing a record absent from
 * the compilation (SDK terminals are proven acyclic and dropped), and
 * `reachesLazy` marks a node that might re-export a lazily imported binding.
 */
struct ImportGraphView {
    std::unordered_map<std::string, uint32_t> nodeIds;
    std::vector<std::vector<uint32_t>> succs;
    std::vector<std::vector<uint32_t>> preds;
    std::vector<uint8_t> reachesUnknownSource;
    std::vector<uint8_t> reachesLazy;
};

ImportGraphView BuildGraphView(const ImportGraph &graph)
{
    const size_t nodeCount = graph.size();
    ImportGraphView view;
    view.nodeIds.reserve(nodeCount);
    uint32_t nextId = 0U;
    for (const auto &[name, node] : graph) {
        view.nodeIds.emplace(name, nextId++);
    }
    view.succs.resize(nodeCount);
    view.preds.resize(nodeCount);
    view.reachesUnknownSource.assign(nodeCount, 0U);
    view.reachesLazy.assign(nodeCount, 0U);
    for (const auto &[name, node] : graph) {
        uint32_t u = view.nodeIds.find(name)->second;
        if (node.mightReexportLazy) {
            view.reachesLazy[u] = 1U;
        }
        for (const auto &src : node.sources) {
            auto it = view.nodeIds.find(src);
            if (it != view.nodeIds.end()) {
                view.succs[u].push_back(it->second);
                view.preds[it->second].push_back(u);
            } else if (!IsSdkRecord(src)) {
                view.reachesUnknownSource[u] = 1U;
            }
        }
    }
    return view;
}

constexpr uint32_t NO_NODE_ID = UINT32_MAX;

/*
 * Iterative Tarjan SCC over the id-indexed graph. compId is equal for two
 * nodes exactly when each can reach the other — which is the "imports back"
 * question the cycle verdict needs.
 */
class TarjanSCC {
public:
    explicit TarjanSCC(const std::vector<std::vector<uint32_t>> &succs) : succs_(succs) {}

    std::vector<uint32_t> Run()
    {
        const size_t nodeCount = succs_.size();
        compId_.assign(nodeCount, NO_NODE_ID);
        index_.assign(nodeCount, NO_NODE_ID);
        low_.assign(nodeCount, 0U);
        onStack_.assign(nodeCount, 0U);
        stack_.reserve(nodeCount);
        for (uint32_t root = 0U; root < nodeCount; ++root) {
            if (index_[root] != NO_NODE_ID) {
                continue;
            }
            Start(root);
            while (!frames_.empty()) {
                Step();
            }
        }
        return compId_;
    }

private:
    void Start(uint32_t root)
    {
        index_[root] = low_[root] = counter_++;
        stack_.push_back(root);
        onStack_[root] = 1U;
        frames_.emplace_back(root, 0U);
    }

    // Explores v's next child: push it when unvisited, else fold its index
    // into v's low link when it still sits on the Tarjan stack.
    void Visit(uint32_t w)
    {
        if (index_[w] != NO_NODE_ID) {
            if (onStack_[w] != 0U) {
                uint32_t v = frames_.back().first;
                low_[v] = std::min(low_[v], index_[w]);
            }
            return;
        }
        index_[w] = low_[w] = counter_++;
        stack_.push_back(w);
        onStack_[w] = 1U;
        frames_.emplace_back(w, 0U);
    }

    // Advances v's current frame; finishes v (and its component) when all
    // children are explored.
    void Step()
    {
        uint32_t v = frames_.back().first;
        size_t ci = frames_.back().second;
        if (ci < succs_[v].size()) {
            frames_.back().second = ci + 1U;
            Visit(succs_[v][ci]);
            return;
        }
        Finish(v);
    }

    void Finish(uint32_t v)
    {
        if (low_[v] == index_[v]) {
            compCounter_ = PopComponent(v, compCounter_);
        }
        frames_.pop_back();
        if (!frames_.empty()) {
            uint32_t parent = frames_.back().first;
            low_[parent] = std::min(low_[parent], low_[v]);
        }
    }

    /*
     * Pops the finished component rooted at `root` off the Tarjan stack and
     * assigns it `comp`; returns the next free component id. `root` is always
     * present on the stack (it was pushed when discovered), so the loop
     * terminates at the latest on `root` itself.
     */
    uint32_t PopComponent(uint32_t root, uint32_t comp)
    {
        /*
         * root is guaranteed to be on the stack here: vertices leave the
         * stack only via this call (which also assigns their component), and
         * a non-root vertex is never popped before its own Finish — its low
         * link folds in the on-stack SCC root, so low_ != index_ there.
         */
        ASSERT(!stack_.empty());
        uint32_t w = 0U;
        do {
            w = stack_.back();
            stack_.pop_back();
            onStack_[w] = 0U;
            compId_[w] = comp;
        } while (w != root);
        return comp + 1U;
    }

    const std::vector<std::vector<uint32_t>> &succs_;
    std::vector<uint32_t> compId_;
    std::vector<uint32_t> index_;
    std::vector<uint32_t> low_;
    std::vector<uint8_t> onStack_;
    std::vector<uint32_t> stack_;
    std::vector<std::pair<uint32_t, size_t>> frames_;  // (node, next child index)
    uint32_t counter_ {0U};
    uint32_t compCounter_ {0U};
};

/*
 * Backward reachability over reverse edges: a node is flagged when it
 * forward-reaches any flagged node, so after one pass the flag marks exactly
 * the nodes whose import chain hits the offending property (a lazy-reexport
 * module, or an absent non-SDK record).
 */
void BackwardPropagate(const std::vector<std::vector<uint32_t>> &preds, std::vector<uint8_t> *flag)
{
    std::vector<uint32_t> stack;
    const size_t nodeCount = preds.size();
    for (uint32_t u = 0U; u < nodeCount; ++u) {
        if ((*flag)[u] != 0U) {
            stack.push_back(u);
        }
    }
    while (!stack.empty()) {
        uint32_t u = stack.back();
        stack.pop_back();
        for (uint32_t p : preds[u]) {
            if ((*flag)[p] == 0U) {
                (*flag)[p] = 1U;
                stack.push_back(p);
            }
        }
    }
}

/*
 * Cache the verdict per import edge: acyclic iff the source cannot reach back
 * to the importer (different SCC), its chain contains no lazy-reexport module
 * and reaches no absent non-SDK record. A source absent from the compilation
 * is resolved directly (SDK -> acyclic, anything else -> conservative cyclic).
 * IsThrowRemovable then answers in O(1).
 */
void CacheVerdicts(ImportGraph *graph, const ImportGraphView &view, const std::vector<uint32_t> &compId)
{
    for (auto &[name, node] : *graph) {
        uint32_t ru = view.nodeIds.find(name)->second;
        for (auto &slotInfo : node.imports) {
            ImportInfo &info = slotInfo.second;
            auto it = view.nodeIds.find(info.sourceRecord);
            if (it == view.nodeIds.end()) {
                info.acyclic = IsSdkRecord(info.sourceRecord);
                continue;
            }
            uint32_t su = it->second;
            info.acyclic =
                compId[su] != compId[ru] && view.reachesLazy[su] == 0U && view.reachesUnknownSource[su] == 0U;
        }
    }
}

void AnalyzeImportCycles(ImportGraph *graph)
{
    ImportGraphView view = BuildGraphView(*graph);
    TarjanSCC tarjan(view.succs);
    std::vector<uint32_t> compId = tarjan.Run();
    BackwardPropagate(view.preds, &view.reachesLazy);
    BackwardPropagate(view.preds, &view.reachesUnknownSource);
    CacheVerdicts(graph, view, compId);
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

constexpr uint32_t TRIE_ROOT = 0U;

/*
 * The record a function belongs to: the longest module-record name that is a
 * name prefix followed by '.' (mangled names keep "record." as the prefix).
 * Single-record programs (plain single-file compiles) emit unmangled names;
 * the caller covers those with a `fallback` pointer.
 *
 * A trie over the graph's record names makes each query linear in the
 * function name's length and independent of the module count — the previous
 * implementation rescanned the whole graph with a prefix compare per record
 * for EVERY function, i.e. O(#functions * #modules * name-length), which
 * dominates on large merge-abc inputs where both counts are in the thousands.
 */
class OwnerRecordResolver {
public:
    explicit OwnerRecordResolver(const ImportGraph &graph) : nodes_(1U)
    {
        for (const auto &[recName, imports] : graph) {
            (void)imports;
            Insert(recName);
        }
    }

    /*
     * Longest record name that is a prefix of `funcName`, followed by '.' with
     * at least one further character (`funcName.size() > len + 1`, matching
     * the previous linear scan). nullptr when nothing matches.
     */
    const std::string *Resolve(const std::string &funcName) const
    {
        const std::string *best = nullptr;
        uint32_t cur = TRIE_ROOT;
        for (size_t i = 0U; i < funcName.size(); ++i) {
            auto it = nodes_[cur].children.find(funcName[i]);
            if (it == nodes_[cur].children.end()) {
                break;
            }
            cur = it->second;
            if (nodes_[cur].owner != nullptr && funcName.size() > i + 2U && funcName[i + 1U] == '.') {
                best = nodes_[cur].owner;  // walking forward: later matches are longer
            }
        }
        return best;
    }

private:
    struct Node {
        std::unordered_map<char, uint32_t> children;
        const std::string *owner {nullptr};
    };

    void Insert(const std::string &recName)
    {
        uint32_t cur = TRIE_ROOT;
        for (char c : recName) {
            auto it = nodes_[cur].children.find(c);
            if (it != nodes_[cur].children.end()) {
                cur = it->second;
                continue;
            }
            uint32_t next = static_cast<uint32_t>(nodes_.size());
            nodes_.emplace_back();
            nodes_[cur].children.emplace(c, next);  // re-indexed: emplace_back may reallocate nodes_
            cur = next;
        }
        nodes_[cur].owner = &recName;  // the graph key outlives the resolver
    }

    std::vector<Node> nodes_;
};

/*
 * --- Must-definition dataflow over the linear ins sequence ---
 * Like the optimizer's must-availability analysis, but on pandasm ins: track,
 * per program point, the unique (must) origin of the accumulator and of each
 * register. Origin is either "external import slot N" (the value was produced
 * by ldexternalmodulevar N) or unknown. At a merge point the origins must
 * agree on every incoming path, otherwise they degrade to unknown.
 */

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
    /*
     * External slots proven non-hole by a prior throw that executed without
     * firing on every incoming path (must-set; Rule T). Exception edges are
     * not modeled — a catch entry starts from all-unknown — so a gen inside
     * a try region cannot pollute the handler.
     */
    std::unordered_set<uint32_t> proven;

    bool operator==(const FlowState &other) const
    {
        return acc == other.acc && regs == other.regs && proven == other.proven;
    }
};

// ldexternalmodulevar: the acc origin becomes the loaded external slot (or
// unknown when the immediate does not carry a slot index).
void TransferExternalLoad(const Ins &ins, FlowState *state)
{
    auto imms = ins.Imms();
    if (!imms.empty() && std::holds_alternative<int64_t>(imms[0U])) {
        state->acc = ValueOrigin {true, static_cast<uint32_t>(std::get<int64_t>(imms[0U]))};
    } else {
        state->acc = UNKNOWN_ORIGIN;
    }
}

// lda: the acc origin becomes the source register's origin (or unknown when
// that register holds no tracked origin).
void TransferLda(const Ins &ins, FlowState *state)
{
    auto regs = ins.Regs();
    if (!regs.empty()) {
        auto it = state->regs.find(regs[0U]);
        state->acc = (it != state->regs.end()) ? it->second : UNKNOWN_ORIGIN;
    } else {
        state->acc = UNKNOWN_ORIGIN;
    }
}

/*
 * Instruction transfer. Only the register-defining instructions of the ES
 * subset are modeled (sta/mov); every other acc-writing instruction sets the
 * acc origin to unknown, which is the conservative lattice bottom. The
 * external-load and lda cases are extracted to keep this switch within the
 * function-size limit.
 *
 * Regs()/Imms() are virtual and return vectors BY VALUE (one heap allocation
 * each), and this runs per instruction per dataflow iteration — so operands
 * are fetched lazily, only in the branches that need them. The default branch
 * (the vast majority of opcodes) performs no allocation at all.
 */
void TransferIns(const Ins &ins, FlowState &state)
{
    switch (ins.GetOpcode()) {
        case Opcode::LDEXTERNALMODULEVAR:
        case Opcode::WIDE_LDEXTERNALMODULEVAR:
            TransferExternalLoad(ins, &state);
            return;
        case Opcode::THROW_UNDEFINEDIFHOLEWITHNAME:
            /*
             * Rule T: reaching here means the throw executed without firing
             * (a firing throw never continues), so the checked slot is
             * non-hole from this point on.
             */
            if (state.acc.known) {
                state.proven.insert(state.acc.slot);
            }
            return;
        case Opcode::LDA:
            TransferLda(ins, &state);
            return;
        case Opcode::STA: {
            auto regs = ins.Regs();
            if (!regs.empty()) {
                state.regs[regs[0U]] = state.acc;  // acc passes through unchanged
            }
            return;
        }
        case Opcode::MOV: {
            auto regs = ins.Regs();
            if (!regs.empty()) {
                state.regs[regs[0U]] = UNKNOWN_ORIGIN;  // parameter copy, not a load
            }
            return;
        }
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

/*
 * Must-join of one predecessor OUT state into `state`: fields that agree on
 * every incoming path keep their value, everything else degrades to unknown.
 * Intersects in place — the previous implementation first deep-copied every
 * predecessor's OUT (each carrying an unordered_map/set) into a temporary
 * vector, which dominated the dataflow's allocation cost.
 */
void IntersectMustState(FlowState *state, const FlowState &other)
{
    if (state->acc != other.acc) {
        state->acc = UNKNOWN_ORIGIN;
    }
    for (auto it = state->regs.begin(); it != state->regs.end();) {
        auto o = other.regs.find(it->first);
        if (o == other.regs.end() || !(o->second == it->second)) {
            it = state->regs.erase(it);
        } else {
            ++it;
        }
    }
    // Registers known only in `other` are not must-known.
    for (auto it = state->proven.begin(); it != state->proven.end();) {
        if (other.proven.find(*it) == other.proven.end()) {
            it = state->proven.erase(it);
        } else {
            ++it;
        }
    }
}

FlowState ComputeBlockIn(const BasicBlock &bb, const std::vector<BasicBlock> &blocks)
{
    /*
     * The entry's first iteration has no prior throw executed; the fresh-start
     * state joins as an all-unknown identity (it erases every known origin and
     * proven slot), so the entry block IN is always empty — a back-edge into
     * the entry (e.g. a top-level do-while whose loop head IS the entry) cannot
     * pollute `proven` with a throw that only executed on later iterations —
     * otherwise Rule T removes a TDZ throw that must fire.
     */
    if (bb.catchEntry || bb.preds.empty() || bb.isEntry) {
        return FlowState {};
    }
    FlowState in = blocks[bb.preds[0U]].out;
    for (size_t j = 1U; j < bb.preds.size(); ++j) {
        IntersectMustState(&in, blocks[bb.preds[j]].out);
    }
    return in;
}

struct ThrowCheckCtx {
    const std::map<uint32_t, ImportInfo> &imports;
    const FlowState &cur;
};

bool IsThrowRemovable(uint32_t slot, const Ins &ins, const ThrowCheckCtx &ctx)
{
    if (ins.Ids().empty()) {
        return false;
    }
    auto it = ctx.imports.find(slot);
    // Acyclicity was precomputed per import edge by AnalyzeImportCycles; the
    // per-throw check is a cached-flag read, not a graph walk.
    bool acyclic = it != ctx.imports.end() && it->second.localName == ins.Ids()[0U] && it->second.acyclic;
    return acyclic || ctx.cur.proven.count(slot) != 0U;
}

/*
 * Enqueues `bb`'s non-empty successors after `bb`'s OUT changed, so their
 * IN/OUT states get recomputed on the next worklist pass.
 */
void EnqueueSuccessors(const BasicBlock &bb, const std::vector<BasicBlock> &blocks, std::vector<uint8_t> *queued,
                       std::vector<size_t> *worklist)
{
    for (auto s : bb.succs) {
        if ((*queued)[s] == 0U && blocks[s].begin != blocks[s].end) {
            (*queued)[s] = 1U;
            worklist->push_back(s);
        }
    }
}

/*
 * Forward must-dataflow to fixpoint. A worklist replaces the previous
 * round-robin sweep: a block is re-evaluated only when a predecessor's OUT
 * changed (or on the initial seeding). The queue is FIFO — blocks drain in
 * program order, so a straight-line chain of N blocks settles in ~2N
 * transfers instead of the quadratic re-solving a LIFO order causes on such
 * chains. The transfer and join functions are monotone, so the reached
 * fixpoint is the same one the round-robin iteration computed.
 */
void SolveDataflow(std::vector<BasicBlock> &blocks, const Function &func)
{
    std::vector<uint8_t> queued(blocks.size(), 0U);
    std::vector<size_t> worklist;
    worklist.reserve(blocks.size());
    for (size_t b = 0U; b < blocks.size(); ++b) {
        if (blocks[b].begin == blocks[b].end) {
            continue;
        }
        queued[b] = 1U;
        worklist.push_back(b);
    }
    size_t head = 0U;
    while (head < worklist.size()) {
        const size_t b = worklist[head];
        ++head;
        queued[b] = 0U;
        auto &bb = blocks[b];
        FlowState in = ComputeBlockIn(bb, blocks);
        FlowState out = in;
        for (size_t i = bb.begin; i < bb.end; ++i) {
            TransferIns(*func.ins[i], out);
        }
        if (!(in == bb.in) || !(out == bb.out) || !bb.outValid) {
            bb.in = std::move(in);
            bb.out = std::move(out);
            bb.outValid = true;
            // Only successors depend on this block's OUT.
            EnqueueSuccessors(bb, blocks, &queued, &worklist);
        }
    }
}

std::vector<size_t> FindRemovableThrows(const std::vector<BasicBlock> &blocks, const Function &func,
                                        const std::map<uint32_t, ImportInfo> &imports)
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
            if (isThrow && IsThrowRemovable(cur.acc.slot, *ins, {imports, cur})) {
                toRemove.push_back(i);
            }
            TransferIns(*ins, cur);
        }
    }
    return toRemove;
}

/*
 * Marks instructions for removal in `removed`: every throw in `toRemove`,
 * plus each throw's feeding `ldexternalmodulevar` when the next surviving
 * instruction overwrites acc without reading it (or the function ends) — the
 * load result is dead once the throw is gone.
 */
void MarkRemovedLoads(Function &func, const std::vector<size_t> &toRemove, std::vector<bool> *removed)
{
    for (auto idx : toRemove) {
        (*removed)[idx] = true;
    }
    for (auto idx : toRemove) {
        auto prev = static_cast<std::ptrdiff_t>(idx) - 1;
        while (prev >= 0 && (*removed)[static_cast<size_t>(prev)]) {
            --prev;
        }
        if (prev < 0 || !IsExternalLoad(*func.ins[static_cast<size_t>(prev)])) {
            continue;
        }
        size_t next = idx + 1U;
        while (next < func.ins.size() && (*removed)[next]) {
            ++next;
        }
        if (next >= func.ins.size() || OverwritesAccWithoutRead(*func.ins[next])) {
            (*removed)[static_cast<size_t>(prev)] = true;
            LOG(DEBUG, ASSEMBLER) << "EliminateCrossModuleHoleChecks removes dead external load in " << func.name;
        }
    }
}

/*
 * Compacts func.ins in a single forward pass (the previous back-to-front
 * erase loop shifted the tail once per removal) and records the old-to-new
 * instruction index mapping so local_variable_debug entries — whose
 * start/length reference instruction positions — stay valid after compaction.
 * Without the remap, stale insn_order values can exceed ins.size() and cause
 * out-of-bounds access in BuildLineNumberProgram (assembly-emitter.cpp).
 */
void CompactIns(Function &func, const std::vector<bool> &removed)
{
    const size_t old_size = func.ins.size();
    std::vector<size_t> old_to_new(old_size + 1U);
    size_t new_idx = 0;
    for (size_t i = 0; i < old_size; ++i) {
        old_to_new[i] = new_idx;
        if (removed[i]) {
            continue;
        }
        if (new_idx != i) {
            func.ins[new_idx] = std::move(func.ins[i]);
        }
        ++new_idx;
    }
    old_to_new[old_size] = new_idx;
    func.ins.resize(new_idx);
    for (auto &v : func.local_variable_debug) {
        size_t old_start = std::min(static_cast<size_t>(v.start), old_size);
        size_t old_end = std::min(static_cast<size_t>(v.start) + static_cast<size_t>(v.length), old_size);
        v.start = static_cast<uint32_t>(old_to_new[old_start]);
        v.length = static_cast<uint32_t>(old_to_new[old_end] - old_to_new[old_start]);
    }
}

void RemoveDeadLoads(Function &func, const std::vector<size_t> &toRemove)
{
    if (toRemove.empty()) {
        return;
    }
    std::vector<bool> removed(func.ins.size(), false);
    MarkRemovedLoads(func, toRemove, &removed);
    CompactIns(func, removed);
}

void EliminateInFunction(Function &func, const std::map<uint32_t, ImportInfo> &imports)
{
    CfgBuilder builder(func);
    if (builder.Conservative()) {
        return;
    }
    auto &blocks = builder.Blocks();
    SolveDataflow(blocks, func);
    auto toRemove = FindRemovableThrows(blocks, func, imports);
    if (toRemove.empty()) {
        return;
    }
    RemoveDeadLoads(func, toRemove);
}

/*
 * Per-function elimination task. Everything the task reads — the import
 * graph, its cached cycle verdicts, the owner trie — is frozen read-only
 * input by the time tasks are dispatched, and the only thing a task mutates
 * is its own Function object, so concurrent tasks share no mutable state.
 */
struct EliminateTask {
    Function *func {nullptr};
    const std::map<uint32_t, ImportInfo> *imports {nullptr};
};

class EliminateFunctionJob : public panda::WorkerJob {
public:
    EliminateFunctionJob(Function &func, const std::map<uint32_t, ImportInfo> &imports) : func_(func), imports_(imports)
    {
    }
    NO_COPY_SEMANTIC(EliminateFunctionJob);
    NO_MOVE_SEMANTIC(EliminateFunctionJob);
    ~EliminateFunctionJob() override = default;

    bool Run() override
    {
        EliminateInFunction(func_, imports_);
        return true;
    }

private:
    Function &func_;
    const std::map<uint32_t, ImportInfo> &imports_;
};

class EliminateFunctionsQueue : public panda::WorkerQueue {
public:
    EliminateFunctionsQueue(size_t threadCount, const std::vector<EliminateTask> &tasks)
        : WorkerQueue(threadCount), tasks_(tasks)
    {
    }
    NO_COPY_SEMANTIC(EliminateFunctionsQueue);
    NO_MOVE_SEMANTIC(EliminateFunctionsQueue);
    ~EliminateFunctionsQueue() override = default;

    void Schedule() override
    {
        ASSERT(jobsCount_ == 0U);
        std::unique_lock<std::mutex> lock(m_);
        jobs_.reserve(tasks_.size());
        for (const auto &task : tasks_) {
            jobs_.push_back(new EliminateFunctionJob(*task.func, *task.imports));
            ++jobsCount_;
        }
        lock.unlock();
        jobsAvailable_.notify_all();
    }

private:
    const std::vector<EliminateTask> &tasks_;
};

}  // namespace

void EliminateCrossModuleHoleChecks(const std::vector<Program *> &progs, size_t threadCount)
{
    ImportGraph graph;
    std::vector<ProgramModuleNames> progModules(progs.size());
    for (size_t idx = 0U; idx < progs.size(); ++idx) {
        CollectProgramImports(*progs[idx], &graph, &progModules[idx]);
    }
    if (graph.empty()) {
        return;
    }
    // One-shot O(V + E) cycle analysis; verdicts are cached per import edge.
    AnalyzeImportCycles(&graph);
    OwnerRecordResolver resolver(graph);
    /*
     * Materialize the per-function tasks. From this point on the graph-side
     * data is read-only, and each task's writes are confined to its own
     * Function — worker threads cannot interfere with each other, and the
     * result does not depend on the scheduling order.
     */
    std::vector<EliminateTask> tasks;
    for (size_t idx = 0U; idx < progs.size(); ++idx) {
        auto *prog = progs[idx];
        // Single-module programs (plain single-file compiles) emit unmangled
        // function names; those belong to the program's only module record.
        const std::string *fallback = (progModules[idx].count == 1U) ? &progModules[idx].last : nullptr;
        for (auto &[name, func] : prog->function_table) {
            auto *record = resolver.Resolve(name);
            if (record == nullptr) {
                record = fallback;
            }
            if (record == nullptr) {
                continue;
            }
            auto it = graph.find(*record);
            if (it != graph.end()) {
                tasks.push_back({&func, &it->second.imports});
            }
        }
    }
    if (tasks.empty()) {
        return;
    }
    if (threadCount <= 1U) {
        for (const auto &task : tasks) {
            EliminateInFunction(*task.func, *task.imports);
        }
        return;
    }
    EliminateFunctionsQueue queue(threadCount, tasks);
    queue.Schedule();
    queue.Consume();
    queue.Wait();
}

}  // namespace panda::pandasm
