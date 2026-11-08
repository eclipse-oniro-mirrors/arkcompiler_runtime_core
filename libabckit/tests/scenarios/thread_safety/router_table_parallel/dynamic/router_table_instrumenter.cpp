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

#include "router_table_instrumenter.h"

namespace libabckit::test {
namespace {

void SetFirstError(RouterTableTransformResult &result, AbckitStatus status)
{
    if (result.status == ABCKIT_STATUS_NO_ERROR && status != ABCKIT_STATUS_NO_ERROR) {
        result.status = status;
    }
}

bool CaptureLastError(const AbckitApi *impl, RouterTableTransformResult &result)
{
    SetFirstError(result, impl->getLastError());
    return result.status == ABCKIT_STATUS_NO_ERROR;
}

template <class T>
bool CheckPointer(T *value, const AbckitApi *impl, RouterTableTransformResult &result)
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

struct EmptyArrayFinder {
    const AbckitApi *impl = nullptr;
    const AbckitGraphApi *implG = nullptr;
    const AbckitIsaApiDynamic *dynG = nullptr;
    RouterTableTransformResult *result = nullptr;
    AbckitInst *createEmptyArray = nullptr;
};

bool FindCreateEmptyArray(AbckitBasicBlock *basicBlock, void *data)
{
    auto &finder = *static_cast<EmptyArrayFinder *>(data);
    auto *inst = finder.implG->bbGetFirstInst(basicBlock);
    if (!CaptureLastError(finder.impl, *finder.result)) {
        return false;
    }
    while (inst != nullptr) {
        auto opcode = finder.dynG->iGetOpcode(inst);
        if (!CaptureLastError(finder.impl, *finder.result)) {
            return false;
        }
        if (opcode == ABCKIT_ISA_API_DYNAMIC_OPCODE_CREATEEMPTYARRAY) {
            if (finder.createEmptyArray != nullptr) {
                SetFirstError(*finder.result, ABCKIT_STATUS_INTERNAL_ERROR);
                return false;
            }
            finder.createEmptyArray = inst;
        }
        inst = finder.implG->iGetNext(inst);
        if (!CaptureLastError(finder.impl, *finder.result)) {
            return false;
        }
    }
    return true;
}

bool InsertAfter(AbckitInst *inst, AbckitInst *cursor, const AbckitGraphApi *implG, const AbckitApi *impl,
                 RouterTableTransformResult &result)
{
    implG->iInsertAfter(inst, cursor);
    return CaptureLastError(impl, result);
}

}  // namespace

RouterTableInstrumenter::RouterTableInstrumenter(AbckitApiVersion version)
{
    impl_ = AbckitGetApiImpl(version);
    implI_ = AbckitGetInspectApiImpl(version);
    implM_ = AbckitGetModifyApiImpl(version);
    implG_ = AbckitGetGraphApiImpl(version);
    dynG_ = AbckitGetIsaApiDynamicImpl(version);
    arkI_ = AbckitGetArktsInspectApiImpl(version);
    arkM_ = AbckitGetArktsModifyApiImpl(version);
}

RouterTableTransformResult RouterTableInstrumenter::PrepareMetadata(RouterTableTask &task) const
{
    RouterTableTransformResult result;
    if (task.routerModule == nullptr || task.initializer == nullptr || task.entries.empty() || impl_ == nullptr ||
        implI_ == nullptr || implM_ == nullptr || implG_ == nullptr || dynG_ == nullptr || arkI_ == nullptr ||
        arkM_ == nullptr) {
        result.status = ABCKIT_STATUS_BAD_ARGUMENT;
        return result;
    }

    auto *routerModule = arkI_->coreModuleToArktsModule(task.routerModule);
    if (!CheckPointer(routerModule, impl_, result)) {
        return result;
    }
    for (auto &entry : task.entries) {
        if (entry.handlerModule == nullptr || entry.handlerClass == nullptr || entry.annotation == nullptr ||
            entry.className == nullptr || entry.classNameText.empty() || entry.fullPath.empty()) {
            SetFirstError(result, ABCKIT_STATUS_BAD_ARGUMENT);
            return result;
        }
        auto *handlerModule = arkI_->coreModuleToArktsModule(entry.handlerModule);
        if (!CheckPointer(handlerModule, impl_, result)) {
            return result;
        }
        AbckitArktsImportFromDynamicModuleCreateParams params {};
        params.name = entry.classNameText.c_str();
        params.alias = entry.classNameText.c_str();
        auto *arktsImport = arkM_->moduleAddImportFromArktsV1ToArktsV1(routerModule, handlerModule, &params);
        if (!CheckPointer(arktsImport, impl_, result)) {
            return result;
        }
        entry.import = arkI_->arktsImportDescriptorToCoreImportDescriptor(arktsImport);
        if (!CheckPointer(entry.import, impl_, result)) {
            return result;
        }
        ++result.importsAdded;

        auto *handlerClass = arkI_->coreClassToArktsClass(entry.handlerClass);
        if (!CheckPointer(handlerClass, impl_, result)) {
            return result;
        }
        auto *annotation = arkI_->coreAnnotationToArktsAnnotation(entry.annotation);
        if (!CheckPointer(annotation, impl_, result)) {
            return result;
        }
        arkM_->classRemoveAnnotation(handlerClass, annotation);
        if (!CaptureLastError(impl_, result)) {
            return result;
        }
        ++result.annotationsRemoved;
    }
    return result;
}

RouterTableTransformResult RouterTableInstrumenter::BuildTable(const RouterTableTask &task) const
{
    RouterTableTransformResult result;
    if (task.initializer == nullptr || task.entries.empty() || impl_ == nullptr || implI_ == nullptr ||
        implM_ == nullptr || implG_ == nullptr || dynG_ == nullptr) {
        result.status = ABCKIT_STATUS_BAD_ARGUMENT;
        return result;
    }
    for (const auto &entry : task.entries) {
        if (entry.import == nullptr || entry.className == nullptr || entry.fullPath.empty()) {
            result.status = ABCKIT_STATUS_BAD_ARGUMENT;
            return result;
        }
    }

    auto *file = implI_->functionGetFile(task.initializer);
    if (!CheckPointer(file, impl_, result)) {
        return result;
    }

    auto *graph = implI_->createGraphFromFunction(task.initializer);
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
    result.tablesProcessed = 1;

    bool graphReady = [&]() {
        EmptyArrayFinder finder {impl_, implG_, dynG_, &result, nullptr};
        auto visited = implG_->gVisitBlocksRpo(graph, &finder, FindCreateEmptyArray);
        if (!CaptureLastError(impl_, result) || !visited || finder.createEmptyArray == nullptr) {
            SetFirstError(result, ABCKIT_STATUS_INTERNAL_ERROR);
            return false;
        }

        AbckitInst *cursor = finder.createEmptyArray;
        uint64_t routeIndex = 0;
        for (const auto &entry : task.entries) {
            auto *path = implM_->createLiteralString(file, entry.fullPath.data(), entry.fullPath.size());
            if (!CheckPointer(path, impl_, result)) {
                return false;
            }
            AbckitLiteral *elements[] = {path};
            auto *route = implM_->createLiteralArray(file, elements, 1U);
            if (!CheckPointer(route, impl_, result)) {
                return false;
            }
            auto *createRoute = dynG_->iCreateCreatearraywithbuffer(graph, route);
            if (!CheckPointer(createRoute, impl_, result)) {
                return false;
            }
            auto *loadHandler = dynG_->iCreateLdexternalmodulevar(graph, entry.import);
            if (!CheckPointer(loadHandler, impl_, result)) {
                return false;
            }
            auto *checkHandler = dynG_->iCreateThrowUndefinedifholewithname(graph, loadHandler, entry.className);
            if (!CheckPointer(checkHandler, impl_, result)) {
                return false;
            }
            auto *newHandler = dynG_->iCreateNewobjrange(graph, 1, loadHandler);
            if (!CheckPointer(newHandler, impl_, result)) {
                return false;
            }
            auto *storeHandler = dynG_->iCreateStownbyindex(graph, newHandler, createRoute, 1U);
            if (!CheckPointer(storeHandler, impl_, result)) {
                return false;
            }
            auto *storeRoute = dynG_->iCreateStownbyindex(graph, createRoute, finder.createEmptyArray, routeIndex++);
            if (!CheckPointer(storeRoute, impl_, result)) {
                return false;
            }

            if (!InsertAfter(createRoute, cursor, implG_, impl_, result) ||
                !InsertAfter(loadHandler, createRoute, implG_, impl_, result) ||
                !InsertAfter(checkHandler, loadHandler, implG_, impl_, result) ||
                !InsertAfter(newHandler, checkHandler, implG_, impl_, result) ||
                !InsertAfter(storeHandler, newHandler, implG_, impl_, result) ||
                !InsertAfter(storeRoute, storeHandler, implG_, impl_, result)) {
                return false;
            }
            cursor = storeRoute;
            ++result.routesInserted;
        }
        return true;
    }();

    if (graphReady) {
        implM_->functionSetGraph(task.initializer, graph);
        if (CaptureLastError(impl_, result)) {
            result.tablesCommitted = 1;
        }
    }
    impl_->destroyGraph(graph);
    CaptureLastError(impl_, result);
    return result;
}

}  // namespace libabckit::test
