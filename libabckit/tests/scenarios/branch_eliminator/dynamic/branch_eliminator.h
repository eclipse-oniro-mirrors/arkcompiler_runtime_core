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

#ifndef BRANCH_ELIMINATOR_H
#define BRANCH_ELIMINATOR_H

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "helpers/visit_helper/visit_helper-inl.h"
#include "libabckit/c/abckit.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/c/isa/isa_dynamic.h"

struct ConstantInfo {
    std::string path;
    std::string objName;    // Config
    std::string fieldName;  // isDebug
    bool fieldValue;        // field is constant true or false
};

using ConstantInfoIndexType = uint32_t;

using SuspectsType = std::unordered_map<AbckitCoreImportDescriptor *, std::vector<ConstantInfoIndexType>>;

struct BranchEliminatorTask {
    std::string moduleName;
    std::string functionName;
    AbckitCoreFunction *function = nullptr;
    SuspectsType suspects;
};

struct BranchEliminatorResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t processedFunctions = 0;
    size_t replacedConfigLoads = 0;
    size_t originalTargetBranches = 0;
    size_t remainingTargetBranches = 0;
    size_t committedFunctions = 0;
};

namespace libabckit::test {
class AbckitBranchEliminatorFailureTest;
}

class BranchEliminator {
public:
    BranchEliminator(enum AbckitApiVersion version, AbckitFile *file, std::vector<ConstantInfo> constants);

    std::vector<BranchEliminatorTask> CollectTasks();
    BranchEliminatorResult RunTask(const BranchEliminatorTask &task);
    AbckitStatus GetStatus() const;
    void Run();

    constexpr static ConstantInfoIndexType INVALID_INDEX = std::numeric_limits<ConstantInfoIndexType>::max();

private:
    friend class libabckit::test::AbckitBranchEliminatorFailureTest;

    bool GetInstAsBool(AbckitInst *inst);
    bool GetSuspects(AbckitCoreModule *mod, SuspectsType &suspects);
    ConstantInfoIndexType GetConstantInfoIndex(AbckitInst *inst, const SuspectsType &suspects);
    void ReplaceUsers(AbckitInst *oldInst, AbckitInst *newInst);
    size_t ReplaceModuleVarByConstant(AbckitGraph *graph, const SuspectsType &suspects);
    void ReplaceLdObjByNameWithBoolean(AbckitInst *ldObjByName, ConstantInfoIndexType constInfoIndex);
    bool DeleteUnreachableBranch(AbckitInst *ifInst);
    bool LoweringConstants(AbckitGraph *graph);
    bool RemoveUnusedInsts(AbckitGraph *graph);
    bool CanBeRemoved(AbckitInst *inst);
    bool IsEliminatableIfInst(AbckitInst *ifInst);
    size_t CountBranches(AbckitGraph *graph);
    std::string_view GetString(AbckitString *str);
    void SetFirstError(AbckitStatus error);
    bool RecordLastError();

private:
    const AbckitApi *impl_ = nullptr;
    const AbckitInspectApi *implI_ = nullptr;
    const AbckitGraphApi *implG_ = nullptr;
    const AbckitIsaApiDynamic *dynG_ = nullptr;
    const AbckitModifyApi *implM_ = nullptr;
    AbckitFile *file_ = nullptr;
    VisitHelper vh_;

    std::vector<ConstantInfo> constants_;
    AbckitStatus status_ = ABCKIT_STATUS_NO_ERROR;
};

#endif /* BRANCH_ELIMINATOR_H */
