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

#include "add_log_instrumenter.h"

#include <vector>

#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"

namespace libabckit::test {
namespace {

void SetFirstError(AddLogTransformResult &result, AbckitStatus status)
{
    if (result.status == ABCKIT_STATUS_NO_ERROR && status != ABCKIT_STATUS_NO_ERROR) {
        result.status = status;
    }
}

bool CaptureLastError(const AbckitApi *impl, AddLogTransformResult &result)
{
    SetFirstError(result, impl->getLastError());
    return result.status == ABCKIT_STATUS_NO_ERROR;
}

template <class T>
bool CheckPointer(T *value, const AbckitApi *impl, AddLogTransformResult &result)
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

struct ReturnCollector {
    const AbckitApi *impl = nullptr;
    const AbckitGraphApi *implG = nullptr;
    const AbckitIsaApiDynamic *dynG = nullptr;
    AddLogTransformResult *result = nullptr;
    std::vector<AbckitInst *> returns;
};

bool CollectReturnInstructions(AbckitBasicBlock *basicBlock, void *data)
{
    auto &collector = *static_cast<ReturnCollector *>(data);
    auto *inst = collector.implG->bbGetFirstInst(basicBlock);
    if (!CaptureLastError(collector.impl, *collector.result)) {
        return false;
    }
    while (inst != nullptr) {
        auto opcode = collector.dynG->iGetOpcode(inst);
        if (!CaptureLastError(collector.impl, *collector.result)) {
            return false;
        }
        if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_RETURNUNDEFINED) {
            collector.returns.push_back(inst);
        }
        inst = collector.implG->iGetNext(inst);
        if (!CaptureLastError(collector.impl, *collector.result)) {
            return false;
        }
    }
    return true;
}

bool InsertAfter(AbckitInst *inst, AbckitInst *after, const AbckitGraphApi *implG, const AbckitApi *impl,
                 AddLogTransformResult &result)
{
    implG->iInsertAfter(inst, after);
    return CaptureLastError(impl, result);
}

}  // namespace

AddLogInstrumenter::AddLogInstrumenter(AbckitApiVersion version, AddLogStrings strings) : strings_(strings)
{
    impl_ = AbckitGetApiImpl(version);
    implI_ = AbckitGetInspectApiImpl(version);
    implM_ = AbckitGetModifyApiImpl(version);
    implG_ = AbckitGetGraphApiImpl(version);
    dynG_ = AbckitGetIsaApiDynamicImpl(version);
}

AddLogTransformResult AddLogInstrumenter::Instrument(AbckitCoreFunction *function, AbckitString *entryMessage) const
{
    AddLogTransformResult result;
    if (function == nullptr || entryMessage == nullptr || strings_.print == nullptr || strings_.date == nullptr ||
        strings_.getTime == nullptr || strings_.elapsed == nullptr || impl_ == nullptr || implI_ == nullptr ||
        implM_ == nullptr || implG_ == nullptr || dynG_ == nullptr) {
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
        ReturnCollector collector {impl_, implG_, dynG_, &result, {}};
        auto visited = implG_->gVisitBlocksRpo(graph, &collector, CollectReturnInstructions);
        if (!CaptureLastError(impl_, result) || !visited || collector.returns.empty()) {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }

        auto *startBlock = implG_->gGetStartBasicBlock(graph);
        if (!CheckPointer(startBlock, impl_, result)) {
            return false;
        }
        std::vector<AbckitBasicBlock *> successors;
        auto successorsVisited =
            implG_->bbVisitSuccBlocks(startBlock, &successors, [](AbckitBasicBlock *successor, void *data) {
                static_cast<std::vector<AbckitBasicBlock *> *>(data)->push_back(successor);
                return true;
            });
        if (!CaptureLastError(impl_, result) || !successorsVisited || successors.size() != 1U) {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }

        auto *entry = dynG_->iCreateLoadString(graph, entryMessage);
        if (!CheckPointer(entry, impl_, result)) {
            return false;
        }
        auto *print = dynG_->iCreateTryldglobalbyname(graph, strings_.print);
        if (!CheckPointer(print, impl_, result)) {
            return false;
        }
        auto *printEntry = dynG_->iCreateCallarg1(graph, print, entry);
        if (!CheckPointer(printEntry, impl_, result)) {
            return false;
        }
        auto *dateClass = dynG_->iCreateTryldglobalbyname(graph, strings_.date);
        if (!CheckPointer(dateClass, impl_, result)) {
            return false;
        }
        auto *dateObject = dynG_->iCreateNewobjrange(graph, 1, dateClass);
        if (!CheckPointer(dateObject, impl_, result)) {
            return false;
        }
        auto *getTime = dynG_->iCreateLdobjbyname(graph, dateObject, strings_.getTime);
        if (!CheckPointer(getTime, impl_, result)) {
            return false;
        }
        auto *startTime = dynG_->iCreateCallthis0(graph, getTime, dateObject);
        if (!CheckPointer(startTime, impl_, result)) {
            return false;
        }

        implG_->bbAddInstFront(successors.front(), entry);
        if (!CaptureLastError(impl_, result) || !InsertAfter(print, entry, implG_, impl_, result) ||
            !InsertAfter(printEntry, print, implG_, impl_, result) ||
            !InsertAfter(dateClass, printEntry, implG_, impl_, result) ||
            !InsertAfter(dateObject, dateClass, implG_, impl_, result) ||
            !InsertAfter(getTime, dateObject, implG_, impl_, result) ||
            !InsertAfter(startTime, getTime, implG_, impl_, result)) {
            return false;
        }

        for (auto *returnInst : collector.returns) {
            auto *endDateClass = dynG_->iCreateTryldglobalbyname(graph, strings_.date);
            if (!CheckPointer(endDateClass, impl_, result)) {
                return false;
            }
            auto *endDateObject = dynG_->iCreateNewobjrange(graph, 1, endDateClass);
            if (!CheckPointer(endDateObject, impl_, result)) {
                return false;
            }
            auto *endGetTime = dynG_->iCreateLdobjbyname(graph, endDateObject, strings_.getTime);
            if (!CheckPointer(endGetTime, impl_, result)) {
                return false;
            }
            auto *endTime = dynG_->iCreateCallthis0(graph, endGetTime, endDateObject);
            if (!CheckPointer(endTime, impl_, result)) {
                return false;
            }
            auto *elapsed = dynG_->iCreateLoadString(graph, strings_.elapsed);
            if (!CheckPointer(elapsed, impl_, result)) {
                return false;
            }
            auto *elapsedPrint = dynG_->iCreateTryldglobalbyname(graph, strings_.print);
            if (!CheckPointer(elapsedPrint, impl_, result)) {
                return false;
            }
            auto *printElapsed = dynG_->iCreateCallarg1(graph, elapsedPrint, elapsed);
            if (!CheckPointer(printElapsed, impl_, result)) {
                return false;
            }
            auto *duration = dynG_->iCreateSub2(graph, startTime, endTime);
            if (!CheckPointer(duration, impl_, result)) {
                return false;
            }
            auto *durationPrint = dynG_->iCreateTryldglobalbyname(graph, strings_.print);
            if (!CheckPointer(durationPrint, impl_, result)) {
                return false;
            }
            auto *printDuration = dynG_->iCreateCallarg1(graph, durationPrint, duration);
            if (!CheckPointer(printDuration, impl_, result)) {
                return false;
            }

            implG_->iInsertBefore(endDateClass, returnInst);
            if (!CaptureLastError(impl_, result) || !InsertAfter(endDateObject, endDateClass, implG_, impl_, result) ||
                !InsertAfter(endGetTime, endDateObject, implG_, impl_, result) ||
                !InsertAfter(endTime, endGetTime, implG_, impl_, result) ||
                !InsertAfter(elapsed, endTime, implG_, impl_, result) ||
                !InsertAfter(elapsedPrint, elapsed, implG_, impl_, result) ||
                !InsertAfter(printElapsed, elapsedPrint, implG_, impl_, result) ||
                !InsertAfter(duration, printElapsed, implG_, impl_, result) ||
                !InsertAfter(durationPrint, duration, implG_, impl_, result) ||
                !InsertAfter(printDuration, durationPrint, implG_, impl_, result)) {
                return false;
            }
            ++result.instrumentedReturns;
        }
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
