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

#include "libabckit/c/abckit.h"

#include "branch_eliminator.h"

BranchEliminator::BranchEliminator(enum AbckitApiVersion version, AbckitFile *file, std::vector<ConstantInfo> constants)
    : file_(file), constants_(std::move(constants))
{
    impl_ = AbckitGetApiImpl(version);
    implI_ = AbckitGetInspectApiImpl(version);
    implG_ = AbckitGetGraphApiImpl(version);
    dynG_ = AbckitGetIsaApiDynamicImpl(version);
    implM_ = AbckitGetModifyApiImpl(version);
    vh_ = VisitHelper(file_, impl_, implI_, implG_, dynG_);
}

void BranchEliminator::Run()
{
    auto tasks = CollectTasks();
    if (status_ != ABCKIT_STATUS_NO_ERROR) {
        return;
    }
    for (const auto &task : tasks) {
        if (RunTask(task).status != ABCKIT_STATUS_NO_ERROR) {
            return;
        }
    }
}

AbckitStatus BranchEliminator::GetStatus() const
{
    return status_;
}

std::vector<BranchEliminatorTask> BranchEliminator::CollectTasks()
{
    std::vector<BranchEliminatorTask> tasks;
    status_ = ABCKIT_STATUS_NO_ERROR;
    if (constants_.empty()) {
        return tasks;
    }

    vh_.EnumerateModules([&](AbckitCoreModule *mod) {
        SuspectsType suspects;
        if (!GetSuspects(mod, suspects)) {
            return;
        }
        auto moduleName = std::string(GetString(implI_->moduleGetName(mod)));
        vh_.EnumerateModuleFunctions(mod, [&](AbckitCoreFunction *func) {
            auto functionName = std::string(GetString(implI_->functionGetName(func)));
            if (status_ == ABCKIT_STATUS_NO_ERROR) {
                tasks.push_back({moduleName, functionName, func, suspects});
            }
        });
    });
    RecordLastError();
    return tasks;
}

BranchEliminatorResult BranchEliminator::RunTask(const BranchEliminatorTask &task)
{
    BranchEliminatorResult result;
    status_ = ABCKIT_STATUS_NO_ERROR;
    if (task.function == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        result.status = status_;
        return result;
    }

    auto *graph = implI_->createGraphFromFunction(task.function);
    RecordLastError();
    if (graph == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        result.status = status_;
        return result;
    }
    if (status_ != ABCKIT_STATUS_NO_ERROR) {
        impl_->destroyGraph(graph);
        RecordLastError();
        result.status = status_;
        return result;
    }

    result.originalTargetBranches = CountBranches(graph);
    if (status_ == ABCKIT_STATUS_NO_ERROR) {
        result.replacedConfigLoads = ReplaceModuleVarByConstant(graph, task.suspects);
    }
    bool hasGraphChanged = result.replacedConfigLoads != 0;
    while (status_ == ABCKIT_STATUS_NO_ERROR) {
        hasGraphChanged |= LoweringConstants(graph);
        if (status_ != ABCKIT_STATUS_NO_ERROR) {
            break;
        }

        auto *ifInst = vh_.GraphInstsFindIf(graph, [&](AbckitInst *inst) { return IsEliminatableIfInst(inst); });
        RecordLastError();
        if (ifInst == nullptr) {
            break;
        }
        if (!DeleteUnreachableBranch(ifInst)) {
            break;
        }
        hasGraphChanged = true;
    }

    if (status_ == ABCKIT_STATUS_NO_ERROR) {
        result.remainingTargetBranches = CountBranches(graph);
    }
    if (status_ == ABCKIT_STATUS_NO_ERROR && hasGraphChanged) {
        implM_->functionSetGraph(task.function, graph);
        if (RecordLastError()) {
            result.committedFunctions = 1;
        }
    }

    impl_->destroyGraph(graph);
    RecordLastError();
    if (status_ == ABCKIT_STATUS_NO_ERROR) {
        result.processedFunctions = 1;
    }
    result.status = status_;
    return result;
}

bool BranchEliminator::GetSuspects(AbckitCoreModule *mod, SuspectsType &suspects)
{
    vh_.EnumerateModuleImports(mod, [&](AbckitCoreImportDescriptor *id) {
        auto importName = GetString(implI_->importDescriptorGetName(id));
        auto *importedModule = implI_->importDescriptorGetImportedModule(id);
        if (!RecordLastError() || importedModule == nullptr) {
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
            return;
        }
        auto path = GetString(implI_->moduleGetName(importedModule));
        for (ConstantInfoIndexType i = 0; i < constants_.size(); ++i) {
            const auto &constInfo = constants_[i];
            if (constInfo.path != path || constInfo.objName != importName) {
                continue;
            }
            auto iter = suspects.find(id);
            if (iter == suspects.end()) {
                std::vector<ConstantInfoIndexType> vec = {i};
                suspects.emplace(id, vec);
            } else {
                iter->second.push_back(i);
            }
        }
    });
    RecordLastError();
    return status_ == ABCKIT_STATUS_NO_ERROR && !suspects.empty();
}

bool BranchEliminator::IsEliminatableIfInst(AbckitInst *inst)
{
    auto opcode = dynG_->iGetOpcode(inst);
    if (!RecordLastError() || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_IF) {
        return false;
    }
    auto inputCount = implG_->iGetInputCount(inst);
    if (!RecordLastError() || inputCount != 2U) {
        return false;
    }
    auto *ldBool = implG_->iGetInput(inst, 0);
    if (!RecordLastError() || ldBool == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    auto op = dynG_->iGetOpcode(ldBool);
    if (!RecordLastError() ||
        (op != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDTRUE && op != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDFALSE)) {
        return false;
    }
    auto *constInst = implG_->iGetInput(inst, 1);
    if (!RecordLastError() || constInst == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    opcode = dynG_->iGetOpcode(constInst);
    return RecordLastError() && opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_CONSTANT;
}

size_t BranchEliminator::ReplaceModuleVarByConstant(AbckitGraph *graph, const SuspectsType &suspects)
{
    // find the following patter:
    // ...
    // 1. ldExternalModuleVar (importdescriptor)
    // 2. ldObjByName v1, "isDebug"
    //
    // and replace it by
    // 3. ldtrue

    std::unordered_map<AbckitInst *, ConstantInfoIndexType> moduleVars;
    vh_.EnumerateGraphInsts(graph, [&](AbckitInst *inst) {
        auto constInfoIndex = GetConstantInfoIndex(inst, suspects);
        if (constInfoIndex != INVALID_INDEX) {
            moduleVars.emplace(inst, constInfoIndex);
        }
    });
    RecordLastError();
    size_t replaced = 0;
    for (auto &[ldObjByName, constInfoIndex] : moduleVars) {
        if (ldObjByName == nullptr || constInfoIndex == INVALID_INDEX || constInfoIndex >= constants_.size()) {
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
            break;
        }
        ReplaceLdObjByNameWithBoolean(ldObjByName, constInfoIndex);
        if (status_ != ABCKIT_STATUS_NO_ERROR) {
            break;
        }
        ++replaced;
    }

    return replaced;
}

ConstantInfoIndexType BranchEliminator::GetConstantInfoIndex(AbckitInst *inst, const SuspectsType &suspects)
{
    auto opcode = dynG_->iGetOpcode(inst);
    if (!RecordLastError() || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDOBJBYNAME) {
        return INVALID_INDEX;
    }
    auto *ldExternalModuleVar = implG_->iGetInput(inst, 0);
    if (!RecordLastError() || ldExternalModuleVar == nullptr) {
        return INVALID_INDEX;
    }
    opcode = dynG_->iGetOpcode(ldExternalModuleVar);
    if (!RecordLastError() || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR) {
        return INVALID_INDEX;
    }
    auto *id = dynG_->iGetImportDescriptor(ldExternalModuleVar);
    if (!RecordLastError() || id == nullptr) {
        return INVALID_INDEX;
    }
    auto iter = suspects.find(id);
    if (iter == suspects.end()) {
        return INVALID_INDEX;
    }
    const auto &constInfoIndexes = iter->second;
    const auto propName = GetString(implG_->iGetString(inst));  // "isDebug"
    for (auto index : constInfoIndexes) {
        if (index >= constants_.size()) {
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
            return INVALID_INDEX;
        }
        if (constants_[index].fieldName == propName) {
            return index;
        }
    }

    return INVALID_INDEX;
}

void BranchEliminator::ReplaceLdObjByNameWithBoolean(AbckitInst *ldObjByName, ConstantInfoIndexType constInfoIndex)
{
    bool fieldVal = constants_[constInfoIndex].fieldValue;
    auto *bb = implG_->iGetBasicBlock(ldObjByName);
    if (!RecordLastError() || bb == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return;
    }
    auto *graph = implG_->bbGetGraph(bb);
    if (!RecordLastError() || graph == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return;
    }
    auto *value = fieldVal ? dynG_->iCreateLdtrue(graph) : dynG_->iCreateLdfalse(graph);
    if (!RecordLastError() || value == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return;
    }
    implG_->iInsertAfter(value, ldObjByName);
    if (!RecordLastError()) {
        return;
    }
    ReplaceUsers(ldObjByName, value);
    if (status_ != ABCKIT_STATUS_NO_ERROR) {
        return;
    }

    auto *ldExternalModuleVar = implG_->iGetInput(ldObjByName, 0);
    if (!RecordLastError() || ldExternalModuleVar == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return;
    }
    bool isRemovable = true;
    std::vector<AbckitInst *> checks;
    vh_.EnumerateInstUsers(ldExternalModuleVar, [&](AbckitInst *inst) {
        if (inst != ldObjByName) {
            auto opcode = dynG_->iGetOpcode(inst);
            if (!RecordLastError()) {
                return;
            }
            if (opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_THROW_UNDEFINEDIFHOLEWITHNAME) {
                isRemovable = false;
            } else {
                checks.emplace_back(inst);
            }
        }
    });
    if (!RecordLastError()) {
        return;
    }

    if (isRemovable) {
        for (auto *check : checks) {
            implG_->iRemove(check);
            if (!RecordLastError()) {
                return;
            }
        }
        implG_->iRemove(ldExternalModuleVar);
        if (!RecordLastError()) {
            return;
        }
    }
    implG_->iRemove(ldObjByName);
    RecordLastError();
}

void BranchEliminator::ReplaceUsers(AbckitInst *oldInst, AbckitInst *newInst)
{
    vh_.EnumerateInstUsers(oldInst, [&](AbckitInst *user) {
        auto inputCount = implG_->iGetInputCount(user);
        if (!RecordLastError()) {
            return;
        }
        for (uint64_t i = 0; i < inputCount; ++i) {
            auto *input = implG_->iGetInput(user, i);
            if (!RecordLastError()) {
                return;
            }
            if (input == oldInst) {
                implG_->iSetInput(user, newInst, i);
                if (!RecordLastError()) {
                    return;
                }
            }
        }
    });
    RecordLastError();
}

bool BranchEliminator::GetInstAsBool(AbckitInst *inst)
{
    auto opcode = dynG_->iGetOpcode(inst);
    if (!RecordLastError()) {
        return false;
    }
    switch (opcode) {
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_CONSTANT: {
            auto value = implG_->iGetConstantValueI64(inst);
            RecordLastError();
            return value != 0;
        }
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_LDTRUE:
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_ISTRUE:
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_ISTRUE:
            return true;
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_LDFALSE:
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_ISFALSE:
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_ISFALSE:
            return false;
        default:
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
    }
}

bool BranchEliminator::DeleteUnreachableBranch(AbckitInst *ifInst)
{
    // compute result of If compare
    auto *ldBool = implG_->iGetInput(ifInst, 0);  // 0: ldtrue or ldfalse
    if (!RecordLastError() || ldBool == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    auto *constInst = implG_->iGetInput(ifInst, 1);  // 1: ConstantInst
    if (!RecordLastError() || constInst == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    auto valLeft = GetInstAsBool(ldBool);
    auto valRight = GetInstAsBool(constInst);
    if (status_ != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto conditionCode = dynG_->iGetConditionCode(ifInst);
    if (!RecordLastError()) {
        return false;
    }

    if (conditionCode != ABCKIT_ISA_API_DYNAMIC_CONDITION_CODE_CC_EQ &&
        conditionCode != ABCKIT_ISA_API_DYNAMIC_CONDITION_CODE_CC_NE) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    bool result = (conditionCode == ABCKIT_ISA_API_DYNAMIC_CONDITION_CODE_CC_EQ && valLeft == valRight) ||
                  (conditionCode == ABCKIT_ISA_API_DYNAMIC_CONDITION_CODE_CC_NE && valLeft != valRight);

    auto *bb = implG_->iGetBasicBlock(ifInst);
    if (!RecordLastError() || bb == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    auto *graph = implG_->bbGetGraph(bb);
    if (!RecordLastError() || graph == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }
    // if true ==> delete false branch
    implG_->iRemove(ifInst);
    if (!RecordLastError()) {
        return false;
    }

    implG_->bbDisconnectSuccBlock(bb, result ? 1 : 0);
    if (!RecordLastError()) {
        return false;
    }
    implG_->gRunPassRemoveUnreachableBlocks(graph);
    return RecordLastError();
}

bool BranchEliminator::LoweringConstants(AbckitGraph *graph)
{
    bool hasGraphChanged = false;
    auto doCb = [this, graph, &hasGraphChanged]() -> bool {
        std::vector<AbckitInst *> users;

        auto instFindUsers = [this, &users](AbckitInst *user) {
            auto userOp = dynG_->iGetOpcode(user);
            if (!RecordLastError()) {
                return;
            }
            if (userOp == ABCKIT_ISA_API_DYNAMIC_OPCODE_ISTRUE || userOp == ABCKIT_ISA_API_DYNAMIC_OPCODE_ISFALSE ||
                userOp == ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_ISTRUE ||
                userOp == ABCKIT_ISA_API_DYNAMIC_OPCODE_CALLRUNTIME_ISFALSE) {
                users.emplace_back(user);
            }
        };

        AbckitInst *ldBool = vh_.GraphInstsFindIf(graph, [&](AbckitInst *inst) {
            auto op = dynG_->iGetOpcode(inst);
            if (!RecordLastError()) {
                return false;
            }
            if (op != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDTRUE && op != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDFALSE) {
                return false;
            }

            vh_.EnumerateInstUsers(inst, instFindUsers);

            return !users.empty();
        });
        RecordLastError();

        if (ldBool == nullptr || status_ != ABCKIT_STATUS_NO_ERROR) {
            return false;
        }

        auto ldBoolVal = GetInstAsBool(ldBool);
        for (auto *isBool : users) {
            auto isBoolVal = GetInstAsBool(isBool);
            if (status_ != ABCKIT_STATUS_NO_ERROR) {
                return false;
            }
            auto *ldBool = ldBoolVal == isBoolVal ? dynG_->iCreateLdtrue(graph) : dynG_->iCreateLdfalse(graph);
            if (!RecordLastError() || ldBool == nullptr) {
                SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
                return false;
            }
            implG_->iInsertAfter(ldBool, isBool);
            if (!RecordLastError()) {
                return false;
            }
            ReplaceUsers(isBool, ldBool);
            if (status_ != ABCKIT_STATUS_NO_ERROR) {
                return false;
            }
            implG_->iRemove(isBool);
            if (!RecordLastError()) {
                return false;
            }
        }

        hasGraphChanged = true;

        return true;
    };

    while (doCb()) {
    }

    if (status_ == ABCKIT_STATUS_NO_ERROR) {
        hasGraphChanged |= RemoveUnusedInsts(graph);
    }

    return hasGraphChanged;
}

bool BranchEliminator::RemoveUnusedInsts(AbckitGraph *graph)
{
    bool hasGraphChanged = false;
    // CC-OFFNXT(G.CTL.03) implementation feature
    do {
        std::vector<AbckitInst *> removableInsts;
        vh_.EnumerateGraphInsts(graph, [&](AbckitInst *inst) {
            auto userCount = implG_->iGetUserCount(inst);
            if (!RecordLastError()) {
                return;
            }
            if (userCount == 0 && CanBeRemoved(inst)) {
                removableInsts.emplace_back(inst);
            }
        });
        RecordLastError();

        if (removableInsts.empty() || status_ != ABCKIT_STATUS_NO_ERROR) {
            break;
        }

        for (auto *inst : removableInsts) {
            implG_->iRemove(inst);
            if (!RecordLastError()) {
                break;
            }
        }

        if (status_ != ABCKIT_STATUS_NO_ERROR) {
            break;
        }

        hasGraphChanged = true;
    } while (true);

    return hasGraphChanged;
}

bool BranchEliminator::CanBeRemoved(AbckitInst *inst)
{
    auto opcode = dynG_->iGetOpcode(inst);
    if (!RecordLastError()) {
        return false;
    }
    switch (opcode) {
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_CONSTANT:
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_LDTRUE:
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_ISTRUE:
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_LDFALSE:
        case ABCKIT_ISA_API_DYNAMIC_OPCODE_ISFALSE:
            return true;
        default:
            return false;
    }
}

size_t BranchEliminator::CountBranches(AbckitGraph *graph)
{
    size_t branches = 0;
    vh_.EnumerateGraphInsts(graph, [&](AbckitInst *inst) {
        auto opcode = dynG_->iGetOpcode(inst);
        if (!RecordLastError()) {
            return;
        }
        if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_IF) {
            ++branches;
        }
    });
    RecordLastError();
    return branches;
}

std::string_view BranchEliminator::GetString(AbckitString *str)
{
    if (str == nullptr) {
        if (RecordLastError()) {
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        }
        return {};
    }
    auto *value = implI_->abckitStringToString(str);
    if (!RecordLastError() || value == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return {};
    }
    return value;
}

void BranchEliminator::SetFirstError(AbckitStatus error)
{
    if (status_ == ABCKIT_STATUS_NO_ERROR && error != ABCKIT_STATUS_NO_ERROR) {
        status_ = error;
    }
}

bool BranchEliminator::RecordLastError()
{
    SetFirstError(impl_->getLastError());
    return status_ == ABCKIT_STATUS_NO_ERROR;
}
