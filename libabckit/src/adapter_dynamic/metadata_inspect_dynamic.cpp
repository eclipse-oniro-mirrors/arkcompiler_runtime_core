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

#include "libabckit/c/statuses.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/src/adapter_dynamic/abckit_dynamic.h"
#include "libabckit/src/adapter_dynamic/metadata_inspect_dynamic.h"
#include "libabckit/src/adapter_dynamic/metadata_modify_dynamic.h"
#include "libabckit/src/adapter_dynamic/helpers_dynamic.h"
#include "libabckit/src/helpers_common.h"
#include "libabckit/src/macros.h"
#include "libabckit/src/metadata_inspect_impl.h"
#include "libabckit/src/statuses_impl.h"
#include "libabckit/src/wrappers/graph_wrapper/graph_wrapper.h"
#include "libabckit/src/wrappers/abcfile_wrapper.h"
#include "libabckit/src/adapter_static/ir_static.h"
#include "libabckit/src/mem_manager/mem_manager.h"

#include "assembler/assembly-emitter.h"
#include "assembler/annotation.h"
#include "assembler/assembly-literals.h"
#include "assembler/assembly-program.h"
#include "libpandafile/file.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

//供构图使用的共享快照
struct AbckitDynamicGraphCacheEntry {
    const panda::panda_file::File *pandaFile = nullptr;
    AbckitIrInterface *irInterface = nullptr;
    std::unordered_map<std::string, uint32_t> methodOffsets;
    uint64_t structuralGeneration = 0;
    std::unordered_map<AbckitCoreFunction *, uint64_t> functionVersions;
};

namespace libabckit {

// CC-OFFNXT(WordsTool.95) sensitive word conflict
// NOLINTNEXTLINE(google-build-using-namespace)
using namespace panda;

namespace {
std::shared_ptr<AbckitDynamicGraphCacheEntry> MakeDynamicGraphCacheEntry(const panda_file::File *pf,
                                                                         AbckitIrInterface *irInterface,
                                                                         const AbckitFile *file)
{
    auto deleter = [](AbckitDynamicGraphCacheEntry *entry) {
        if (entry == nullptr) {
            return;
        }
        delete entry->irInterface;
        delete entry->pandaFile;
        delete entry;
    };
    // 按顺序释放
    auto entry = std::shared_ptr<AbckitDynamicGraphCacheEntry>(new AbckitDynamicGraphCacheEntry(), deleter);
    entry->pandaFile = pf;
    entry->irInterface = irInterface;
    entry->methodOffsets.reserve(irInterface->GetMethods().size());
    for (const auto &[offset, name] : irInterface->GetMethods()) {
        entry->methodOffsets[name] = offset;
    }
    entry->structuralGeneration = file->dynamicStructuralGeneration.load(std::memory_order_acquire);
    entry->functionVersions = file->dynamicFunctionVersions;
    return entry;
}

uint64_t GetSnapshotFunctionVersion(const AbckitDynamicGraphCacheEntry &entry, AbckitCoreFunction *function)
{
    auto found = entry.functionVersions.find(function);
    return found == entry.functionVersions.end() ? 0 : found->second;
}

bool CanUseSnapshot(const AbckitDynamicGraphCacheEntry &entry, const AbckitFile *file, AbckitCoreFunction *function)
{
    return entry.structuralGeneration == file->dynamicStructuralGeneration.load(std::memory_order_acquire) &&
           GetSnapshotFunctionVersion(entry, function) == GetDynamicGraphFunctionVersion(file, function);
}

void PublishDynamicGraphSnapshotFailure(AbckitFile::DynamicGraphCache &cache, uint64_t buildEpoch)
{
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.entry.reset();
        cache.state = cache.invalidationEpoch == buildEpoch ? AbckitDynamicGraphSnapshotState::FAILED
                                                            : AbckitDynamicGraphSnapshotState::STALE;
    }
    cache.cv.notify_all();
}

struct AcquiredDynamicGraphSnapshot {
    std::shared_ptr<AbckitDynamicGraphCacheEntry> entry;
    uint64_t structuralGeneration = 0;
    uint64_t ownerStructuralGeneration = 0;
    uint64_t functionVersion = 0;
};

AcquiredDynamicGraphSnapshot AcquireDynamicGraphSnapshot(AbckitFile *file, pandasm::Program *program,
                                                         AbckitCoreFunction *function)
{
    // The caller must hold a lifecycle operation token for file until this function returns.
    auto &cache = file->dynamicGraphCache;
    bool waitedForBuilder = false;
    while (true) {
        bool shouldWait = false;
        {
            AbckitFileApiReadLock apiLock(file);
            std::lock_guard<std::mutex> lock(cache.mutex);
            if (cache.state == AbckitDynamicGraphSnapshotState::BUILDING) {
                shouldWait = true;
            } else if (cache.state == AbckitDynamicGraphSnapshotState::READY && cache.entry != nullptr) {
                if (CanUseSnapshot(*cache.entry, file, function)) {
                    return {cache.entry, file->dynamicStructuralGeneration.load(std::memory_order_acquire),
                            GetDynamicStructuralGenerationForThread(file, std::this_thread::get_id()),
                            GetDynamicGraphFunctionVersion(file, function)};
                }
                cache.state = AbckitDynamicGraphSnapshotState::BUILDING;
                break;
            } else if (waitedForBuilder && cache.state == AbckitDynamicGraphSnapshotState::FAILED) {
                statuses::SetLastError(AbckitStatus::ABCKIT_STATUS_INTERNAL_ERROR);
                return {};
            } else {
                cache.state = AbckitDynamicGraphSnapshotState::BUILDING;
                break;
            }
        }

        if (shouldWait) {
            AbckitFileApiReadLockSuspension readLockSuspension(file);
            std::unique_lock<std::mutex> lock(cache.mutex);
            cache.cv.wait(lock, [&cache]() { return cache.state != AbckitDynamicGraphSnapshotState::BUILDING; });
            waitedForBuilder = true;
        }
    }

    AbckitFileApiReadLockSuspension readLockSuspension(file);
    AbckitFileApiWriteLock apiLock(file);
    if (apiLock.HasConflict()) {
        uint64_t buildEpoch;
        {
            std::lock_guard<std::mutex> lock(cache.mutex);
            buildEpoch = cache.invalidationEpoch;
        }
        statuses::SetLastError(AbckitStatus::ABCKIT_STATUS_WRONG_STATE);
        PublishDynamicGraphSnapshotFailure(cache, buildEpoch);
        return {};
    }

    uint64_t buildEpoch;
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        buildEpoch = cache.invalidationEpoch;
    }
    AbckitFile::DynamicGraphCache::BuildStateGuard buildStateGuard(cache, buildEpoch);

    pandasm::AsmEmitter::PandaFileToPandaAsmMaps maps;
    auto *pf = EmitDynamicProgram(file, program, &maps, true);
    if (pf == nullptr) {
        return {};
    }

    auto *irInterface = new AbckitIrInterface(std::move(maps.methods), std::move(maps.fields), std::move(maps.classes),
                                              std::move(maps.strings), std::move(maps.literalarrays));
    auto entry = MakeDynamicGraphCacheEntry(pf, irInterface, file);
    bool published = false;
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        if (cache.invalidationEpoch == buildEpoch) {
            cache.entry = entry;
            cache.state = AbckitDynamicGraphSnapshotState::READY;
            ++cache.successfulBuildCount;
            published = true;
        } else {
            cache.entry.reset();
            cache.state = AbckitDynamicGraphSnapshotState::STALE;
        }
    }
    cache.cv.notify_all();
    buildStateGuard.Release();
    if (!published) {
        statuses::SetLastError(AbckitStatus::ABCKIT_STATUS_WRONG_STATE);
        return {};
    }
    return {entry, file->dynamicStructuralGeneration.load(std::memory_order_acquire),
            GetDynamicStructuralGenerationForThread(file, std::this_thread::get_id()),
            GetDynamicGraphFunctionVersion(file, function)};
}
}  // namespace

// ========================================
// Module
// ========================================

bool ModuleEnumerateAnonymousFunctionsDynamic(AbckitCoreModule *m, void *data,
                                              bool (*cb)(AbckitCoreFunction *function, void *data))
{
    LIBABCKIT_LOG_FUNC;
    LIBABCKIT_BAD_ARGUMENT(m, false)
    LIBABCKIT_BAD_ARGUMENT(cb, false)

    for (auto &function : m->functions) {
        if (!FunctionIsAnonymousDynamic(function.get())) {
            continue;
        }
        if (!cb(function.get(), data)) {
            return false;
        }
    }
    return true;
}

// ========================================
// Namespace
// ========================================

AbckitString *NamespaceGetNameDynamic(AbckitCoreNamespace *n)
{
    LIBABCKIT_LOG_FUNC;
    ASSERT(n->owningModule->target == ABCKIT_TARGET_ARK_TS_V1);
    auto func = GetDynFunction(n->GetArkTSImpl()->f.get());
    auto name = func->name;
    size_t sharpPos = name.rfind('#');
    ASSERT(sharpPos != std::string::npos);
    auto subname = name.substr(sharpPos + 1);
    return CreateStringDynamic(n->owningModule->file, subname.data(), subname.size());
}

// ========================================
// Class
// ========================================

AbckitString *ClassGetNameDynamic(AbckitCoreClass *klass)
{
    LIBABCKIT_LOG_FUNC;
    auto *cachedName = klass->cachedDisplayName.load(std::memory_order_acquire);
    if (cachedName != nullptr) {
        return cachedName;
    }

    auto func = GetDynFunction(klass);
    auto mPayload = GetDynModulePayload(klass->owningModule);

    auto *scopesLitArr = mPayload->scopeNamesLiteralArray;
    auto name = GetClassNameFromCtor(func->name, scopesLitArr);

    auto *result = CreateStringDynamic(klass->owningModule->file, name.data(), name.size());
    klass->cachedDisplayName.store(result, std::memory_order_release);
    return result;
}

// ========================================
// Function
// ========================================

AbckitString *FunctionGetNameDynamic(AbckitCoreFunction *function)
{
    LIBABCKIT_LOG_FUNC;
    auto *cachedName = function->cachedDisplayName.load(std::memory_order_acquire);
    if (cachedName != nullptr) {
        return cachedName;
    }

    auto *functionImpl = GetDynFunction(function);
    auto name = functionImpl->name;

    size_t sharpPos = name.rfind('#');
    if (!function->isAnonymous) {
        if (sharpPos != std::string::npos) {
            name = name.substr(sharpPos + 1);
        } else {
            name = name.substr(name.rfind('.') + 1);
            ASSERT(name == "func_main_0");
        }
    }

    auto *result = CreateStringDynamic(function->owningModule->file, name.data(), name.size());
    function->cachedDisplayName.store(result, std::memory_order_release);
    return result;
}

AbckitGraph *CreateGraphFromFunctionDynamic(AbckitCoreFunction *function)
{
    LIBABCKIT_LOG_FUNC;
    auto *file = function->owningModule->file;
    std::string functionName;
    pandasm::Program *program = nullptr;
    {
        AbckitFileApiReadLock apiLock(file);
        auto *func = GetDynFunction(function);
        functionName = func->name;
        LIBABCKIT_LOG(DEBUG) << functionName << '\n';
        LIBABCKIT_LOG_DUMP(func->DebugDump(), DEBUG);
        program = file->GetDynamicProgram();
    }
    auto acquiredSnapshot = AcquireDynamicGraphSnapshot(file, program, function);
    if (acquiredSnapshot.entry == nullptr) {
        return nullptr;
    }
    auto cacheEntry = acquiredSnapshot.entry;
    auto graphIrInterface = std::make_unique<AbckitIrInterface>(cacheEntry->irInterface);

    uint32_t functionOffset = 0;
    auto methodOffsetIt = cacheEntry->methodOffsets.find(functionName);
    if (methodOffsetIt != cacheEntry->methodOffsets.end()) {
        functionOffset = methodOffsetIt->second;
    }
    if (functionOffset == 0) {
        LIBABCKIT_LOG(DEBUG) << "functionOffset == 0\n";
        statuses::SetLastError(AbckitStatus::ABCKIT_STATUS_INTERNAL_ERROR);
        return nullptr;
    }

    auto wpf = std::make_unique<FileWrapper>(reinterpret_cast<const void *>(cacheEntry->pandaFile), false);
    auto graph =
        GraphWrapper::BuildGraphDynamic(wpf.get(), graphIrInterface.get(), file, functionOffset, true, cacheEntry);
    if (graph == nullptr) {
        return nullptr;
    }
    static_cast<void>(wpf.release());
    static_cast<void>(graphIrInterface.release());
    if (statuses::GetLastError() != AbckitStatus::ABCKIT_STATUS_NO_ERROR) {
        GraphWrapper::DestroyGraphDynamic(graph);
        return nullptr;
    }
    LIBABCKIT_LOG_DUMP(GdumpStatic(graph, STDERR_FILENO), DEBUG);

    ASSERT(graph->file == file);
    graph->function = function;
    graph->versionedFunction = function;
    graph->ownerThreadId = std::this_thread::get_id();
    graph->baseStructuralGeneration = acquiredSnapshot.structuralGeneration;
    graph->baseOwnerStructuralGeneration = acquiredSnapshot.ownerStructuralGeneration;
    graph->baseFunctionVersion = acquiredSnapshot.functionVersion;

    return graph;
}

bool FunctionIsStaticDynamic(AbckitCoreFunction *function)
{
    LIBABCKIT_LOG_FUNC;
    auto *func = GetDynFunction(function);
    return IsStatic(func->name);
}

bool FunctionIsCtorDynamic(AbckitCoreFunction *function)
{
    LIBABCKIT_LOG_FUNC;
    auto *func = GetDynFunction(function);
    return IsCtor(func->name);
}

bool FunctionIsAnonymousDynamic(AbckitCoreFunction *function)
{
    LIBABCKIT_LOG_FUNC;
    return function->isAnonymous;
}

bool FunctionIsNativeDynamic(AbckitCoreFunction *function)
{
    LIBABCKIT_LOG_FUNC;
    auto *func = GetDynFunction(function);
    return (func->metadata->GetAccessFlags() & ACC_NATIVE) != 0x0;
}

// ========================================
// Annotation
// ========================================

AbckitString *AnnotationInterfaceGetNameDynamic(AbckitCoreAnnotationInterface *ai)
{
    LIBABCKIT_LOG_FUNC;
    auto name = pandasm::GetItemName(ai->GetArkTSImpl()->GetDynamicImpl()->name);
    return CreateStringDynamic(ai->owningModule->file, name.data(), name.size());
}

// ========================================
// ImportDescriptor
// ========================================

AbckitString *ImportDescriptorGetNameDynamic(AbckitCoreImportDescriptor *i)
{
    std::string name = [i]() {
        auto mPayload = GetDynModulePayload(i->importingModule);
        auto idPayload = GetDynImportDescriptorPayload(i);
        const auto *moduleLitArr = mPayload->moduleLiteralArray;
        auto sectionOffset =
            idPayload->isRegularImport ? mPayload->regularImportsOffset : mPayload->namespaceImportsOffset;
        if (!idPayload->isRegularImport) {
            return std::string("*");
        }
        auto importNameOffset = sectionOffset + idPayload->moduleRecordIndexOff * 3 + 1;
        return std::get<std::string>(moduleLitArr->GetDynamicImpl()->literals_[importNameOffset].value_);
    }();
    return CreateStringDynamic(i->importingModule->file, name.data(), name.size());
}

AbckitString *ImportDescriptorGetAliasDynamic(AbckitCoreImportDescriptor *i)
{
    std::string name = [i]() {
        auto mPayload = GetDynModulePayload(i->importingModule);
        auto idPayload = GetDynImportDescriptorPayload(i);
        const auto *moduleLitArr = mPayload->moduleLiteralArray;
        auto sectionOffset =
            idPayload->isRegularImport ? mPayload->regularImportsOffset : mPayload->namespaceImportsOffset;
        auto gap = idPayload->isRegularImport ? 3 : 2;
        auto importNameOffset = sectionOffset + idPayload->moduleRecordIndexOff * gap;
        return std::get<std::string>(moduleLitArr->GetDynamicImpl()->literals_[importNameOffset].value_);
    }();
    return CreateStringDynamic(i->importingModule->file, name.data(), name.size());
}

// ========================================
// ExportDescriptor
// ========================================

AbckitString *ExportDescriptorGetNameDynamic(AbckitCoreExportDescriptor *i)
{
    std::string name = [i]() {
        auto mPayload = GetDynModulePayload(i->exportingModule);
        auto edPayload = GetDynExportDescriptorPayload(i);
        const auto *moduleLitArr = mPayload->moduleLiteralArray;
        size_t exportNameOffset;
        switch (edPayload->kind) {
            case AbckitDynamicExportKind::ABCKIT_DYNAMIC_EXPORT_KIND_LOCAL_EXPORT: {
                exportNameOffset = mPayload->localExportsOffset + edPayload->moduleRecordIndexOff * 2 + 1;
                break;
            }
            case AbckitDynamicExportKind::ABCKIT_DYNAMIC_EXPORT_KIND_INDIRECT_EXPORT: {
                exportNameOffset = mPayload->indirectExportsOffset + edPayload->moduleRecordIndexOff * 3 + 1;
                break;
            }
            case AbckitDynamicExportKind::ABCKIT_DYNAMIC_EXPORT_KIND_STAR_EXPORT: {
                std::string res = "*";
                return res;
            }
        }
        return std::get<std::string>(moduleLitArr->GetDynamicImpl()->literals_[exportNameOffset].value_);
    }();
    return CreateStringDynamic(i->exportingModule->file, name.data(), name.size());
}

AbckitString *ExportDescriptorGetAliasDynamic(AbckitCoreExportDescriptor *i)
{
    std::string name = [i]() {
        auto mPayload = GetDynModulePayload(i->exportingModule);
        auto edPayload = GetDynExportDescriptorPayload(i);
        const auto *moduleLitArr = mPayload->moduleLiteralArray;
        size_t exportNameOffset;
        switch (edPayload->kind) {
            case AbckitDynamicExportKind::ABCKIT_DYNAMIC_EXPORT_KIND_LOCAL_EXPORT: {
                exportNameOffset = mPayload->localExportsOffset + edPayload->moduleRecordIndexOff * 2;
                break;
            }
            case AbckitDynamicExportKind::ABCKIT_DYNAMIC_EXPORT_KIND_INDIRECT_EXPORT: {
                exportNameOffset = mPayload->indirectExportsOffset + edPayload->moduleRecordIndexOff * 3;
                break;
            }
            case AbckitDynamicExportKind::ABCKIT_DYNAMIC_EXPORT_KIND_STAR_EXPORT: {
                if (!edPayload->hasServiceImport) {
                    std::string res;
                    return res;
                }
                exportNameOffset = mPayload->localExportsOffset + edPayload->moduleRecordIndexOff * 2 + 1;
            }
        }
        return std::get<std::string>(moduleLitArr->GetDynamicImpl()->literals_[exportNameOffset].value_);
    }();
    return CreateStringDynamic(i->exportingModule->file, name.data(), name.size());
}

// ========================================
// Literal
// ========================================

bool LiteralGetBoolDynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (!literal->IsBoolValue()) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return false;
    }
    return std::get<bool>(literal->value_);
}

uint8_t LiteralGetU8Dynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (!literal->IsByteValue()) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return 0;
    }
    return std::get<uint8_t>(literal->value_);
}

uint16_t LiteralGetU16Dynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (!literal->IsShortValue()) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return 0;
    }
    return std::get<uint16_t>(literal->value_);
}

uint16_t LiteralGetMethodAffiliateDynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (literal->tag_ != panda_file::LiteralTag::METHODAFFILIATE) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return 0;
    }
    return std::get<uint16_t>(literal->value_);
}

uint32_t LiteralGetU32Dynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (!literal->IsIntegerValue()) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return 0;
    }
    return std::get<uint32_t>(literal->value_);
}

uint64_t LiteralGetU64Dynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (!literal->IsLongValue()) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return 0;
    }
    return std::get<uint64_t>(literal->value_);
}

float LiteralGetFloatDynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (!literal->IsFloatValue()) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return 0;
    }
    return std::get<float>(literal->value_);
}

double LiteralGetDoubleDynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (!literal->IsDoubleValue()) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return 0;
    }
    return std::get<double>(literal->value_);
}

AbckitLiteralArray *LiteralGetLiteralArrayDynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (literal->tag_ != panda::panda_file::LiteralTag::LITERALARRAY) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return nullptr;
    }

    auto &litarrs = lit->file->GetDynamicProgram()->literalarray_table;
    auto &arrName = std::get<std::string>(literal->value_);
    auto found = litarrs.find(arrName);
    if (found == litarrs.end()) {
        statuses::SetLastError(ABCKIT_STATUS_INTERNAL_ERROR);
        return nullptr;
    }
    auto *literalArrayImpl = &found->second;

    AbckitFileLitArrsCacheLock cacheLock(lit->file);
    // Search through already created abckit litarrs
    for (auto &item : lit->file->litarrs) {
        if (item->GetDynamicImpl() == literalArrayImpl) {
            return item.get();
        }
    }

    // Create new abckit litarrs
    auto litarr = std::make_unique<AbckitLiteralArray>(lit->file, literalArrayImpl);
    lit->file->litarrs.emplace_back(std::move(litarr));
    return lit->file->litarrs[lit->file->litarrs.size() - 1].get();
}

AbckitString *LiteralGetStringDynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *literal = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    if (!literal->IsStringValue()) {
        statuses::SetLastError(ABCKIT_STATUS_WRONG_LITERAL_TYPE);
        return nullptr;
    }

    auto &val = std::get<std::string>(literal->value_);
    {
        AbckitFileStringsCacheReadLock cacheLock(lit->file);
        auto &strings = lit->file->strings;
        auto found = strings.find(val);
        if (found != strings.end()) {
            return found->second.get();
        }
    }

    AbckitFileStringsCacheWriteLock cacheLock(lit->file);
    auto &strings = lit->file->strings;
    auto found = strings.find(val);
    if (found != strings.end()) {
        return found->second.get();
    }

    auto s = std::make_unique<AbckitString>();
    s->file = lit->file;
    s->impl = val;
    auto result = strings.emplace(val, std::move(s));
    return result.first->second.get();
}

AbckitLiteralTag LiteralGetTagDynamic(AbckitLiteral *lit)
{
    LIBABCKIT_LOG_FUNC;
    auto *litImpl = reinterpret_cast<pandasm::LiteralArray::Literal *>(lit->val.get());
    switch (litImpl->tag_) {
        case panda_file::LiteralTag::ARRAY_U1:
        case panda_file::LiteralTag::BOOL:
            return ABCKIT_LITERAL_TAG_BOOL;
        case panda_file::LiteralTag::ARRAY_U8:
        case panda_file::LiteralTag::ARRAY_I8:
            return ABCKIT_LITERAL_TAG_BYTE;
        case panda_file::LiteralTag::ARRAY_U16:
        case panda_file::LiteralTag::ARRAY_I16:
            return ABCKIT_LITERAL_TAG_SHORT;
        case panda_file::LiteralTag::ARRAY_U32:
        case panda_file::LiteralTag::ARRAY_I32:
        case panda_file::LiteralTag::INTEGER:
            return ABCKIT_LITERAL_TAG_INTEGER;
        case panda_file::LiteralTag::ARRAY_U64:
        case panda_file::LiteralTag::ARRAY_I64:
            return ABCKIT_LITERAL_TAG_LONG;
        case panda_file::LiteralTag::ARRAY_F32:
        case panda_file::LiteralTag::FLOAT:
            return ABCKIT_LITERAL_TAG_FLOAT;
        case panda_file::LiteralTag::ARRAY_F64:
        case panda_file::LiteralTag::DOUBLE:
            return ABCKIT_LITERAL_TAG_DOUBLE;
        case panda_file::LiteralTag::ARRAY_STRING:
        case panda_file::LiteralTag::STRING:
            return ABCKIT_LITERAL_TAG_STRING;
        case panda_file::LiteralTag::NULLVALUE:
            return ABCKIT_LITERAL_TAG_NULLVALUE;
        case panda_file::LiteralTag::TAGVALUE:
            return ABCKIT_LITERAL_TAG_TAGVALUE;
        case panda_file::LiteralTag::METHOD:
        case panda_file::LiteralTag::GETTER:
        case panda_file::LiteralTag::SETTER:
        case panda_file::LiteralTag::GENERATORMETHOD:
        case panda_file::LiteralTag::ASYNCGENERATORMETHOD:
            return ABCKIT_LITERAL_TAG_METHOD;
        case panda_file::LiteralTag::METHODAFFILIATE:
            return ABCKIT_LITERAL_TAG_METHODAFFILIATE;
        case panda_file::LiteralTag::LITERALARRAY:
            return ABCKIT_LITERAL_TAG_LITERALARRAY;
        default:
            return ABCKIT_LITERAL_TAG_INVALID;
    }
}

bool LiteralArrayEnumerateElementsDynamic(AbckitLiteralArray *litArr, void *data,
                                          bool (*cb)(AbckitFile *file, AbckitLiteral *lit, void *data))
{
    LIBABCKIT_LOG_FUNC;
    auto *arrImpl = litArr->GetDynamicImpl();

    size_t size = arrImpl->literals_.size();
    if (size % 2U != 0) {
        statuses::SetLastError(ABCKIT_STATUS_INTERNAL_ERROR);
        return false;
    }

    for (size_t idx = 1; idx < size; idx += 2U) {
        auto litImpl = arrImpl->literals_[idx];
        if (!cb(litArr->file, FindOrCreateLiteralDynamic(litArr->file, litImpl), data)) {
            return false;
        }
    }
    return true;
}

// ========================================
// Value
// ========================================

AbckitType *ValueGetTypeDynamic(AbckitValue *value)
{
    LIBABCKIT_LOG_FUNC;
    auto *pVal = reinterpret_cast<pandasm::ScalarValue *>(value->val.get());
    AbckitTypeId id;
    switch (pVal->GetType()) {
        case pandasm::Value::Type::U1:
            id = ABCKIT_TYPE_ID_U1;
            break;
        case pandasm::Value::Type::U8:
            id = ABCKIT_TYPE_ID_U8;
            break;
        case pandasm::Value::Type::U16:
            id = ABCKIT_TYPE_ID_U16;
            break;
        case pandasm::Value::Type::U32:
            id = ABCKIT_TYPE_ID_U32;
            break;
        case pandasm::Value::Type::U64:
            id = ABCKIT_TYPE_ID_U64;
            break;
        case pandasm::Value::Type::F64:
            id = ABCKIT_TYPE_ID_F64;
            break;
        case pandasm::Value::Type::STRING:
            id = ABCKIT_TYPE_ID_STRING;
            break;
        case pandasm::Value::Type::LITERALARRAY:
            id = ABCKIT_TYPE_ID_LITERALARRAY;
            break;
        default:
            LIBABCKIT_UNIMPLEMENTED;
    }
    // NOTE implement logic for classes
    return GetOrCreateType(value->file, id, 0, nullptr, nullptr);
}

bool ValueGetU1Dynamic(AbckitValue *value)
{
    LIBABCKIT_LOG_FUNC;
    if (ValueGetTypeDynamic(value)->id != ABCKIT_TYPE_ID_U1) {
        statuses::SetLastError(ABCKIT_STATUS_BAD_ARGUMENT);
        return false;
    }

    auto *pVal = reinterpret_cast<pandasm::ScalarValue *>(value->val.get());
    return pVal->GetValue<bool>();
}

double ValueGetDoubleDynamic(AbckitValue *value)
{
    LIBABCKIT_LOG_FUNC;
    if (ValueGetTypeDynamic(value)->id != ABCKIT_TYPE_ID_F64) {
        statuses::SetLastError(ABCKIT_STATUS_BAD_ARGUMENT);
        return 0.0;
    }

    auto *pVal = reinterpret_cast<pandasm::ScalarValue *>(value->val.get());
    return pVal->GetValue<double>();
}

AbckitString *ValueGetStringDynamic(AbckitValue *value)
{
    LIBABCKIT_LOG_FUNC;
    if (ValueGetTypeDynamic(value)->id != ABCKIT_TYPE_ID_STRING) {
        statuses::SetLastError(ABCKIT_STATUS_BAD_ARGUMENT);
        return nullptr;
    }

    auto *pVal = reinterpret_cast<pandasm::ScalarValue *>(value->val.get());
    auto valImpl = pVal->GetValue<std::string>();
    return CreateStringDynamic(value->file, valImpl.data(), valImpl.size());
}

AbckitLiteralArray *ArrayValueGetLiteralArrayDynamic(AbckitValue *value)
{
    LIBABCKIT_LOG_FUNC;
    if (ValueGetTypeDynamic(value)->id != ABCKIT_TYPE_ID_LITERALARRAY) {
        statuses::SetLastError(ABCKIT_STATUS_BAD_ARGUMENT);
        return nullptr;
    }

    auto *pVal = reinterpret_cast<pandasm::ScalarValue *>(value->val.get());
    auto arrName = pVal->GetValue<std::string>();
    auto &litarrs = value->file->GetDynamicProgram()->literalarray_table;
    auto found = litarrs.find(arrName);
    if (found == litarrs.end()) {
        statuses::SetLastError(ABCKIT_STATUS_INTERNAL_ERROR);
        return nullptr;
    }
    auto *literalArrayImpl = &found->second;

    AbckitFileLitArrsCacheLock cacheLock(value->file);
    // Search through already created abckit litarrs
    for (auto &item : value->file->litarrs) {
        if (item->GetDynamicImpl() == literalArrayImpl) {
            return item.get();
        }
    }

    // Create new abckit litarr
    auto litarr = std::make_unique<AbckitLiteralArray>(value->file, literalArrayImpl);
    return value->file->litarrs.emplace_back(std::move(litarr)).get();
}

}  // namespace libabckit
