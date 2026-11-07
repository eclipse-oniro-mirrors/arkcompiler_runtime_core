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

#include "api_modifier.h"
#include "helpers/visit_helper/visit_helper-inl.h"
#include "libabckit/c/metadata_core.h"

#include <algorithm>
#include <cstring>

namespace {

void SetFirstError(ParameterCheckTransformResult &result, AbckitStatus status)
{
    if (result.status == ABCKIT_STATUS_NO_ERROR && status != ABCKIT_STATUS_NO_ERROR) {
        result.status = status;
    }
}

bool CaptureLastError(const AbckitApi *impl, ParameterCheckTransformResult &result)
{
    auto status = impl->getLastError();
    SetFirstError(result, status);
    return status == ABCKIT_STATUS_NO_ERROR;
}

template <class T>
bool CheckPointer(T *value, const AbckitApi *impl, ParameterCheckTransformResult &result)
{
    if (!CaptureLastError(impl, result)) {
        return false;
    }
    if (value != nullptr) {
        return true;
    }
    SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
    return false;
}

}  // namespace

void ApiModifier::Run()
{
    visitor_.EnumerateModules([&](AbckitCoreModule *mod) {
        AbckitCoreImportDescriptor *impDescriptor = GetImportDescriptor(mod);
        if (impDescriptor == nullptr) {
            return;
        }
        visitor_.EnumerateModuleFunctions(mod, [&](AbckitCoreFunction *method) {
            ModifyFunction(method, impDescriptor);
            return true;
        });
    });
}

void ApiModifier::CollectCheckerTasks(std::vector<AbckitCoreFunction *> &tasks)
{
    visitor_.EnumerateModules([&](AbckitCoreModule *mod) {
        AbckitCoreImportDescriptor *impDescriptor = GetImportDescriptor(mod);
        if (impDescriptor == nullptr) {
            return;
        }
        visitor_.EnumerateModuleFunctions(mod, [&](AbckitCoreFunction *method) {
            CollectCheckerTasksForMethod(impDescriptor, method, tasks);
            return true;
        });
    });
}

void ApiModifier::CollectCheckerTasksForMethod(AbckitCoreImportDescriptor *impDescriptor, AbckitCoreFunction *method,
                                               std::vector<AbckitCoreFunction *> &tasks)
{
    visitor_.EnumerateFunctionInsts(method, [&](AbckitInst *inst) {
        AbckitCoreFunction *sub = GetSubclassMethod(impDescriptor, inst);
        if (sub == nullptr) {
            return;
        }
        // Dedup by pointer: a subclass handle may be referenced from more than one site.
        if (std::find(tasks.begin(), tasks.end(), sub) == tasks.end()) {
            tasks.push_back(sub);
        }
    });
}

AbckitCoreImportDescriptor *ApiModifier::GetImportDescriptor(AbckitCoreModule *module)
{
    AbckitCoreImportDescriptor *impDescriptor = nullptr;
    visitor_.EnumerateModuleImports(module, [&](AbckitCoreImportDescriptor *id) {
        auto importName = visitor_.GetString(implI_->importDescriptorGetName(id));
        auto *importedModule = implI_->importDescriptorGetImportedModule(id);
        auto source = visitor_.GetString(implI_->moduleGetName(importedModule));
        if (source != methodInfo_.path) {
            return false;
        }
        if (importName == methodInfo_.className) {
            impDescriptor = id;
            return true;
        }
        return false;
    });
    return impDescriptor;
}

void ApiModifier::ModifyFunction(AbckitCoreFunction *method, AbckitCoreImportDescriptor *id)
{
    visitor_.EnumerateFunctionInsts(method, [&](AbckitInst *inst) {
        auto subclassMethod = GetSubclassMethod(id, inst);
        if (subclassMethod != nullptr) {
            AddParamChecker(subclassMethod);
        }
    });
}

AbckitCoreFunction *ApiModifier::GetSubclassMethod(AbckitCoreImportDescriptor *id, AbckitInst *inst)
{
    if (dynG_->iGetOpcode(inst) != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR) {
        return nullptr;
    }

    if (dynG_->iGetImportDescriptor(inst) != id) {
        return nullptr;
    }

    AbckitCoreFunction *foundMethod = nullptr;
    visitor_.EnumerateInstUsers(inst, [&](AbckitInst *user) {
        if (dynG_->iGetOpcode(user) == ABCKIT_ISA_API_DYNAMIC_OPCODE_DEFINECLASSWITHBUFFER) {
            auto method = implG_->iGetFunction(user);
            auto klass = implI_->functionGetParentClass(method);
            foundMethod = GetMethodToModify(klass);
        }
    });

    return foundMethod;
}

AbckitCoreFunction *ApiModifier::GetMethodToModify(AbckitCoreClass *klass)
{
    AbckitCoreFunction *foundMethod = nullptr;
    visitor_.EnumerateClassMethods(klass, [&](AbckitCoreFunction *method) {
        auto name = visitor_.GetString(implI_->functionGetName(method));
        if (name == methodInfo_.methodName) {
            foundMethod = method;
            return true;
        }
        return false;
    });
    return foundMethod;
}

ParameterCheckTransformResult ApiModifier::AddParamChecker(AbckitCoreFunction *method)
{
    ParameterCheckTransformResult result;
    AbckitGraph *graph = implI_->createGraphFromFunction(method);
    if (!CheckPointer(graph, impl_, result)) {
        return result;
    }

    bool graphReady = [&]() {
        auto *file = implI_->functionGetFile(method);
        if (!CheckPointer(file, impl_, result)) {
            return false;
        }
        AbckitBasicBlock *startBB = implG_->gGetStartBasicBlock(graph);
        if (!CheckPointer(startBB, impl_, result)) {
            return false;
        }
        AbckitInst *idx = implG_->bbGetLastInst(startBB);
        if (!CheckPointer(idx, impl_, result)) {
            return false;
        }
        AbckitInst *arr = implG_->iGetPrev(idx);
        if (!CheckPointer(arr, impl_, result)) {
            return false;
        }

        std::vector<AbckitBasicBlock *> succBBs;
        bool visitResult = implG_->bbVisitSuccBlocks(startBB, &succBBs, [](AbckitBasicBlock *succBasicBlock, void *d) {
            auto *succs = reinterpret_cast<std::vector<AbckitBasicBlock *> *>(d);
            succs->emplace_back(succBasicBlock);
            return true;
        });
        bool visitStatusOk = CaptureLastError(impl_, result);
        if (!visitResult || !visitStatusOk || succBBs.empty()) {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }

        AbckitString *str = implM_->createString(file, "length", strlen("length"));
        if (!CheckPointer(str, impl_, result)) {
            return false;
        }

        AbckitInst *constant = implG_->gFindOrCreateConstantI32(graph, -1);
        if (!CheckPointer(constant, impl_, result)) {
            return false;
        }
        AbckitInst *arrLength = dynG_->iCreateLdobjbyname(graph, arr, str);
        if (!CheckPointer(arrLength, impl_, result)) {
            return false;
        }

        AbckitBasicBlock *trueBB = succBBs[0];
        implG_->bbDisconnectSuccBlock(startBB, ABCKIT_TRUE_SUCC_IDX);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        AbckitBasicBlock *falseBB = implG_->bbCreateEmpty(graph);
        if (!CheckPointer(falseBB, impl_, result)) {
            return false;
        }
        result.createdFailureBlocks++;
        auto *endBB = implG_->gGetEndBasicBlock(graph);
        if (!CheckPointer(endBB, impl_, result)) {
            return false;
        }
        implG_->bbAppendSuccBlock(falseBB, endBB);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        auto *returnInst = dynG_->iCreateReturn(graph, constant);
        if (!CheckPointer(returnInst, impl_, result)) {
            return false;
        }
        implG_->bbAddInstBack(falseBB, returnInst);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        AbckitBasicBlock *ifBB = implG_->bbCreateEmpty(graph);
        if (!CheckPointer(ifBB, impl_, result)) {
            return false;
        }
        result.createdCheckBlocks++;
        AbckitInst *intrinsicGreatereq = dynG_->iCreateGreatereq(graph, arrLength, idx);
        if (!CheckPointer(intrinsicGreatereq, impl_, result)) {
            return false;
        }
        AbckitInst *ifInst = dynG_->iCreateIf(graph, intrinsicGreatereq, ABCKIT_ISA_API_DYNAMIC_CONDITION_CODE_CC_EQ);
        if (!CheckPointer(ifInst, impl_, result)) {
            return false;
        }
        implG_->bbAddInstBack(ifBB, arrLength);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        implG_->bbAddInstBack(ifBB, intrinsicGreatereq);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        implG_->bbAddInstBack(ifBB, ifInst);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        implG_->bbAppendSuccBlock(startBB, ifBB);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        implG_->bbAppendSuccBlock(ifBB, trueBB);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        implG_->bbAppendSuccBlock(ifBB, falseBB);
        return CaptureLastError(impl_, result);
    }();

    if (graphReady) {
        implM_->functionSetGraph(method, graph);
        if (CaptureLastError(impl_, result)) {
            result.committedFunctions = 1;
        }
    }

    impl_->destroyGraph(graph);
    CaptureLastError(impl_, result);
    return result;
}
