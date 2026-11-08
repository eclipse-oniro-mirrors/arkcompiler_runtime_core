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

#include "try_catch_instrumenter.h"

#include <vector>

namespace libabckit::test {
namespace {

void SetFirstError(TryCatchTransformResult &result, AbckitStatus status)
{
    if (result.status == ABCKIT_STATUS_NO_ERROR && status != ABCKIT_STATUS_NO_ERROR) {
        result.status = status;
    }
}

bool CaptureLastError(const AbckitApi *impl, TryCatchTransformResult &result)
{
    SetFirstError(result, impl->getLastError());
    return result.status == ABCKIT_STATUS_NO_ERROR;
}

template <class T>
bool CheckPointer(T *value, const AbckitApi *impl, TryCatchTransformResult &result)
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

bool CollectBlock(AbckitBasicBlock *basicBlock, void *data)
{
    static_cast<std::vector<AbckitBasicBlock *> *>(data)->push_back(basicBlock);
    return true;
}

}  // namespace

TryCatchInstrumenter::TryCatchInstrumenter(AbckitApiVersion version, AbckitString *printName) : printName_(printName)
{
    impl_ = AbckitGetApiImpl(version);
    implI_ = AbckitGetInspectApiImpl(version);
    implM_ = AbckitGetModifyApiImpl(version);
    implG_ = AbckitGetGraphApiImpl(version);
    dynG_ = AbckitGetIsaApiDynamicImpl(version);
}

TryCatchTransformResult TryCatchInstrumenter::Instrument(AbckitCoreFunction *function) const
{
    TryCatchTransformResult result;
    if (function == nullptr || printName_ == nullptr || impl_ == nullptr || implI_ == nullptr || implM_ == nullptr ||
        implG_ == nullptr || dynG_ == nullptr) {
        result.status = ABCKIT_STATUS_BAD_ARGUMENT;
        return result;
    }

    auto *graph = implI_->createGraphFromFunction(function);
    auto createStatus = impl_->getLastError();
    SetFirstError(result, createStatus);
    if (createStatus != ABCKIT_STATUS_NO_ERROR || graph == nullptr) {
        if (graph != nullptr) {
            impl_->destroyGraph(graph);
            SetFirstError(result, impl_->getLastError());
        } else {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
        }
        return result;
    }
    result.processedFunctions = 1;

    bool graphReady = [&]() {
        auto *startBlock = implG_->gGetStartBasicBlock(graph);
        if (!CheckPointer(startBlock, impl_, result)) {
            return false;
        }
        auto *endBlock = implG_->gGetEndBasicBlock(graph);
        if (!CheckPointer(endBlock, impl_, result)) {
            return false;
        }

        std::vector<AbckitBasicBlock *> startSuccessors;
        auto visited = implG_->bbVisitSuccBlocks(startBlock, &startSuccessors, CollectBlock);
        if (!CaptureLastError(impl_, result) || !visited || startSuccessors.size() != 1U) {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        auto *bodyBlock = startSuccessors.front();
        auto *initialInstruction = implG_->bbGetFirstInst(bodyBlock);
        if (!CheckPointer(initialInstruction, impl_, result)) {
            return false;
        }
        auto *returnInstruction = implG_->bbGetLastInst(bodyBlock);
        if (!CheckPointer(returnInstruction, impl_, result)) {
            return false;
        }
        auto returnOpcode = dynG_->iGetOpcode(returnInstruction);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        if (returnOpcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_RETURN) {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        auto *beforeReturn = implG_->iGetPrev(returnInstruction);
        if (!CheckPointer(beforeReturn, impl_, result)) {
            return false;
        }

        auto *initialBlock = implG_->iGetBasicBlock(initialInstruction);
        if (!CheckPointer(initialBlock, impl_, result)) {
            return false;
        }
        auto *tryBegin = implG_->bbSplitBlockAfterInstruction(initialBlock, initialInstruction, true);
        if (!CheckPointer(tryBegin, impl_, result)) {
            return false;
        }
        auto *beforeReturnBlock = implG_->iGetBasicBlock(beforeReturn);
        if (!CheckPointer(beforeReturnBlock, impl_, result)) {
            return false;
        }
        auto *tryEnd = implG_->bbSplitBlockAfterInstruction(beforeReturnBlock, beforeReturn, true);
        if (!CheckPointer(tryEnd, impl_, result)) {
            return false;
        }

        auto *catchBlock = implG_->bbCreateEmpty(graph);
        if (!CheckPointer(catchBlock, impl_, result)) {
            return false;
        }
        auto *catchPhi = implG_->bbCreateCatchPhi(catchBlock, 0);
        if (!CheckPointer(catchPhi, impl_, result)) {
            return false;
        }
        auto *print = dynG_->iCreateTryldglobalbyname(graph, printName_);
        if (!CheckPointer(print, impl_, result)) {
            return false;
        }
        implG_->bbAddInstBack(catchBlock, print);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        auto *printError = dynG_->iCreateCallarg1(graph, print, catchPhi);
        if (!CheckPointer(printError, impl_, result)) {
            return false;
        }
        implG_->bbAddInstBack(catchBlock, printError);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }

        std::vector<AbckitBasicBlock *> endPredecessors;
        visited = implG_->bbVisitPredBlocks(endBlock, &endPredecessors, CollectBlock);
        if (!CaptureLastError(impl_, result) || !visited || endPredecessors.size() != 1U) {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        auto *epilogueBlock = endPredecessors.front();
        auto *epilogueReturn = implG_->bbGetLastInst(epilogueBlock);
        if (!CheckPointer(epilogueReturn, impl_, result)) {
            return false;
        }
        auto inputCount = implG_->iGetInputCount(epilogueReturn);
        if (!CaptureLastError(impl_, result) || inputCount == 0U) {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }
        auto *successValue = implG_->iGetInput(epilogueReturn, 0);
        if (!CheckPointer(successValue, impl_, result)) {
            return false;
        }
        auto *failureValue = dynG_->iCreateLdfalse(graph);
        if (!CheckPointer(failureValue, impl_, result)) {
            return false;
        }
        implG_->bbAddInstBack(bodyBlock, failureValue);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        auto *returnValue = implG_->bbCreatePhi(epilogueBlock, 2, successValue, failureValue);
        if (!CheckPointer(returnValue, impl_, result)) {
            return false;
        }
        implG_->iSetInput(epilogueReturn, returnValue, 0);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }

        implG_->gInsertTryCatch(tryBegin, tryEnd, catchBlock, catchBlock);
        if (!CaptureLastError(impl_, result)) {
            return false;
        }
        result.insertedCatchRegions = 1;
        return true;
    }();

    if (graphReady) {
        implM_->functionSetGraph(function, graph);
        if (CaptureLastError(impl_, result)) {
            result.committedFunctions = 1;
        }
    }
    impl_->destroyGraph(graph);
    CaptureLastError(impl_, result);
    return result;
}

}  // namespace libabckit::test
