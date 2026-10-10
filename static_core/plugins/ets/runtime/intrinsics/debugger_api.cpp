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

#include "plugins/ets/runtime/ets_execution_context.h"
#include "intrinsics.h"
#include "libarkbase/utils/bit_utils.h"
#include "runtime/include/thread_scopes.h"
#include "libarkbase/utils/logger.h"
#include "libarkfile/class_data_accessor.h"
#include "libarkfile/debug_info_extractor.h"
#include "libarkfile/file-inl.h"
#include "libarkfile/file.h"
#include "libarkbase/utils/utf.h"
#include "runtime/include/exceptions.h"
#include "plugins/ets/runtime/ets_exceptions.h"
#include "plugins/ets/runtime/ets_stubs.h"
#include "plugins/ets/runtime/ets_platform_types.h"
#include "plugins/ets/runtime/tooling/helpers.h"
#include "plugins/ets/runtime/types/ets_array.h"
#include "plugins/ets/runtime/types/ets_box_primitive.h"
#include "plugins/ets/runtime/types/ets_field.h"
#include "plugins/ets/runtime/types/ets_method.h"
#include "plugins/ets/runtime/types/ets_reflect_method.h"
#include "plugins/ets/runtime/types/ets_primitives.h"
#include "plugins/ets/runtime/types/ets_std_core_array.h"
#include "plugins/ets/runtime/types/ets_string.h"
#include "runtime/include/class_linker_extension.h"
#include "runtime/include/class_linker.h"
#include "runtime/include/mem/panda_string.h"
#include "runtime/include/stack_walker.h"
#include "runtime/include/tooling/debug_interface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <securec.h>
#include <set>
#include <type_traits>

#include <limits>

namespace ark::ets::intrinsics {

// Defined below; forward-declared for DebuggerAPIGet's class-name fallback.
static EtsClass *ResolveClassByName(const panda_file::File *pandaFile, EtsExecutionContext *executionCtx,
                                    const std::string &className, ClassLinkerContext *context);

static void ThrowDebuggerError(EtsExecutionContext *executionCtx, const std::string &msg)
{
    LOG(ERROR, DEBUGGER) << "ThrowDebuggerError: " << msg;
    auto *pt = PlatformTypes(executionCtx);
    if (pt != nullptr && pt->escompatError != nullptr && pt->coreString != nullptr &&
        pt->escompatErrorOptions != nullptr && ManagedThread::IsManagedScope()) {
        ThrowEtsException(executionCtx, pt->escompatError, msg.c_str());
        return;
    }
    LOG(ERROR, DEBUGGER) << "Debugger error (no exception class available): " << msg;
}

namespace {
class SilentErrorHandler : public ClassLinkerErrorHandler {
    void OnError([[maybe_unused]] ClassLinker::Error error, [[maybe_unused]] const PandaString &message) override {}
};
}  // namespace

static void SetRuntimeException(EtsLong regNumber, EtsExecutionContext *executionCtx, std::string_view typeName)
{
    auto errorMsg =
        "Failed to access variable at vreg #" + std::to_string(regNumber) + " and type " + std::string(typeName);
    ThrowDebuggerError(executionCtx, errorMsg);
}

// Returns true if the box class matches the target primitive type signature.
static bool BoxClassMatchesSig(EtsPlatformTypes const *ptypes, EtsClass *cls, char sig)
{
    switch (sig) {
        case 'Z':
            return cls == ptypes->coreBoolean;
        case 'B':
            return cls == ptypes->coreByte;
        case 'S':
            return cls == ptypes->coreShort;
        case 'C':
            return cls == ptypes->coreChar;
        case 'I':
            return cls == ptypes->coreInt;
        case 'J':
            return cls == ptypes->coreLong;
        case 'F':
            return cls == ptypes->coreFloat;
        case 'D':
            return cls == ptypes->coreDouble;
        default:
            return false;
    }
}

// Unboxes an integral box (byte/short/char/int/long) to int64, returns nullopt otherwise.
static std::optional<int64_t> GetBoxedIntegralValue(EtsPlatformTypes const *ptypes, EtsObject *obj)
{
    auto *cls = obj->GetClass();
    if (cls == ptypes->coreLong) {
        return EtsBoxPrimitive<EtsLong>::Unbox(obj);
    }
    if (cls == ptypes->coreInt) {
        return EtsBoxPrimitive<EtsInt>::Unbox(obj);
    }
    if (cls == ptypes->coreShort) {
        return EtsBoxPrimitive<EtsShort>::Unbox(obj);
    }
    if (cls == ptypes->coreByte) {
        return EtsBoxPrimitive<EtsByte>::Unbox(obj);
    }
    if (cls == ptypes->coreChar) {
        return EtsBoxPrimitive<EtsChar>::Unbox(obj);
    }
    return std::nullopt;
}

// Unboxes any numeric box to double; returns nullopt for non-numeric values.
static std::optional<double> GetBoxedNumericValueAsDouble(EtsPlatformTypes const *ptypes, EtsObject *obj)
{
    auto *cls = obj->GetClass();
    if (cls == ptypes->coreDouble) {
        return EtsBoxPrimitive<EtsDouble>::Unbox(obj);
    }
    if (cls == ptypes->coreFloat) {
        return EtsBoxPrimitive<EtsFloat>::Unbox(obj);
    }
    if (auto integral = GetBoxedIntegralValue(ptypes, obj); integral.has_value()) {
        return static_cast<double>(*integral);
    }
    return std::nullopt;
}

// Converts a boxed value to the vreg representation required by the target type
// signature. When the actual box type differs from the signature (e.g. an Int box
// assigned to a double variable), the value is converted following the ETS widening
// rules; unsupported conversions throw a TypeError and return std::nullopt so the
// caller skips the write. Reinterpreting the box memory as the target type would
// write garbage values (e.g. an Int box read as 8 bytes yields 43 * 2^-1074).
static std::optional<ark::tooling::VRegValue> ConvertWidenedToVRegValue(EtsPlatformTypes const *ptypes,
                                                                        EtsObject *value, EtsClass *cls, char target)
{
    switch (target) {
        case 'D': {
            if (auto v = GetBoxedNumericValueAsDouble(ptypes, value); v.has_value()) {
                return ark::tooling::VRegValue(bit_cast<int64_t>(*v));
            }
            break;
        }
        case 'F': {
            if (auto v = GetBoxedNumericValueAsDouble(ptypes, value); v.has_value()) {
                return ark::tooling::VRegValue(bit_cast<int32_t>(static_cast<float>(*v)));
            }
            break;
        }
        case 'J': {
            if (auto v = GetBoxedIntegralValue(ptypes, value); v.has_value()) {
                return ark::tooling::VRegValue(*v);
            }
            break;
        }
        case 'I': {
            // long/float/double -> int is not an implicit (widening) conversion.
            if (cls != ptypes->coreLong) {
                if (auto v = GetBoxedIntegralValue(ptypes, value); v.has_value()) {
                    return ark::tooling::VRegValue(static_cast<int32_t>(*v));
                }
            }
            break;
        }
        default:
            break;
    }
    return std::nullopt;
}

// Reference-signature write path with a declared-type check. SetVariable
// stores raw bits and compiled code later consumes the vreg under the
// declared type, so an unrelated object would be a type confusion: resolve
// the signature descriptor in the paused method's load context and reject
// values whose class is not assignable to the declared type. null is
// allowed for any reference type. When the descriptor cannot be resolved
// (or no load context is available) the write keeps the legacy unchecked
// behavior to not break exotic signatures.
static std::optional<ark::tooling::VRegValue> ConvertRefValueToVRegValueChecked(EtsExecutionContext *executionCtx,
                                                                                ClassLinkerContext *loadContext,
                                                                                EtsObject *value, std::string_view sig)
{
    // Pin the value: resolving the descriptor may trigger class loading and
    // GC, and a moving collector would invalidate the raw pointer.
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsObject> valueHandle(executionCtx, value);
    if (loadContext != nullptr) {
        auto *linker = Runtime::GetCurrent()->GetClassLinker();
        auto *extension = linker->GetExtension(panda_file::SourceLang::ETS);
        if (extension != nullptr) {
            std::string sigStr(sig);
            SilentErrorHandler silentErr;
            auto *declaredClass = EtsClass::FromRuntimeClass(
                extension->GetClass(reinterpret_cast<const uint8_t *>(sigStr.c_str()), true, loadContext, &silentErr));
            if (declaredClass != nullptr && valueHandle.GetPtr() != nullptr &&
                !declaredClass->IsAssignableFrom(valueHandle->GetClass())) {
                ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreTypeError,
                                  "Cannot assign value to variable of declared reference type: type mismatch");
                return std::nullopt;
            }
        }
    }
    return ark::ets::tooling::EtsObjectToVRegValue(valueHandle.GetPtr(), sig);
}

static std::optional<ark::tooling::VRegValue> ConvertAndUnboxToVRegValue(EtsExecutionContext *executionCtx,
                                                                         ClassLinkerContext *loadContext,
                                                                         EtsObject *value, std::string_view sig)
{
    if (ark::ets::tooling::IsTaggedSignature(sig)) {
        return ark::ets::tooling::EtsObjectToVRegValue(value, sig);
    }
    if (ark::ets::tooling::IsRefSignature(sig)) {
        return ConvertRefValueToVRegValueChecked(executionCtx, loadContext, value, sig);
    }
    if (value == nullptr || ark::ets::tooling::IsNullOrNullValue(value)) {
        return ark::tooling::VRegValue(0);
    }

    auto *ptypes = PlatformTypes(executionCtx);
    auto *cls = value->GetClass();
    char target = sig[0];

    // Exact match: unbox directly.
    if (BoxClassMatchesSig(ptypes, cls, target)) {
        return ark::ets::tooling::EtsObjectToVRegValue(value, sig);
    }

    // Mismatched box type: convert following ETS widening rules.
    auto widened = ConvertWidenedToVRegValue(ptypes, value, cls, target);
    if (widened.has_value()) {
        return widened;
    }
    std::string msg = "Cannot assign value to variable of type signature '";
    msg += target;
    msg += "'";
    ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreTypeError, msg.c_str());
    return std::nullopt;
}

template <typename T>
static T DebuggerAPIGetLocal(EtsExecutionContext *executionCtx, EtsLong regNumber)
{
    uint32_t previousFrameDepth = 1;

    auto *runtime = executionCtx->GetPandaVM()->GetRuntime();
    if (UNLIKELY(!runtime->IsDebugMode())) {
        ThrowDebuggerError(executionCtx, "Debugger is not enabled");
        return static_cast<T>(0);
    }
    if (UNLIKELY(regNumber < 0 || regNumber > std::numeric_limits<int32_t>::max())) {
        SetRuntimeException(regNumber, executionCtx, ark::ets::tooling::EtsTypeName<T>::NAME);
        return static_cast<T>(0);
    }
    if (UNLIKELY(!ManagedThread::IsManagedScope())) {
        SetRuntimeException(regNumber, executionCtx, ark::ets::tooling::EtsTypeName<T>::NAME);
        return static_cast<T>(0);
    }

    ark::tooling::VRegValue vregValue;
    auto &debugger = runtime->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto err = debugger.GetVariable(thread, previousFrameDepth, regNumber, &vregValue);
    if (err) {
        LOG(ERROR, DEBUGGER) << "Failed to get local variable: " << err.value().GetMessage();
        SetRuntimeException(regNumber, executionCtx, ark::ets::tooling::EtsTypeName<T>::NAME);
        return static_cast<T>(0);
    }
    return ark::ets::tooling::VRegValueToEtsValue<T>(vregValue);
}

EtsBoolean DebuggerAPIGetLocalBoolean(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return DebuggerAPIGetLocal<EtsBoolean>(executionCtx, regNumber);
}

EtsByte DebuggerAPIGetLocalByte(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return DebuggerAPIGetLocal<EtsByte>(executionCtx, regNumber);
}

EtsShort DebuggerAPIGetLocalShort(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return DebuggerAPIGetLocal<EtsShort>(executionCtx, regNumber);
}

EtsChar DebuggerAPIGetLocalChar(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return DebuggerAPIGetLocal<EtsChar>(executionCtx, regNumber);
}

EtsInt DebuggerAPIGetLocalInt(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return DebuggerAPIGetLocal<EtsInt>(executionCtx, regNumber);
}

EtsFloat DebuggerAPIGetLocalFloat(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return DebuggerAPIGetLocal<EtsFloat>(executionCtx, regNumber);
}

EtsDouble DebuggerAPIGetLocalDouble(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return DebuggerAPIGetLocal<EtsDouble>(executionCtx, regNumber);
}

EtsLong DebuggerAPIGetLocalLong(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return DebuggerAPIGetLocal<EtsLong>(executionCtx, regNumber);
}

EtsObject *DebuggerAPIGetLocalObject(EtsLong regNumber)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();

    [[maybe_unused]] HandleScope<ObjectHeader *> scope(executionCtx->GetMT());

    auto *obj = DebuggerAPIGetLocal<ObjectHeader *>(executionCtx, regNumber);
    if (obj == nullptr || ark::ets::tooling::IsNullOrNullValue(EtsObject::FromCoreType(obj))) {
        return nullptr;
    }
    VMHandle<ObjectHeader> objHandle(executionCtx->GetMT(), obj);
    return EtsObject::FromCoreType(objHandle.GetPtr());
}

template <typename T>
static void DebuggerAPISetLocal(EtsExecutionContext *executionCtx, EtsLong regNumber, T value)
{
    uint32_t previousFrameDepth = 1;

    auto *runtime = executionCtx->GetPandaVM()->GetRuntime();
    if (UNLIKELY(!runtime->IsDebugMode())) {
        ThrowDebuggerError(executionCtx, "Debugger is not enabled");
        return;
    }
    if (UNLIKELY(regNumber < 0 || regNumber > std::numeric_limits<int32_t>::max())) {
        SetRuntimeException(regNumber, executionCtx, ark::ets::tooling::EtsTypeName<T>::NAME);
        return;
    }
    if (UNLIKELY(!ManagedThread::IsManagedScope())) {
        SetRuntimeException(regNumber, executionCtx, ark::ets::tooling::EtsTypeName<T>::NAME);
        return;
    }

    auto vregValue = ark::ets::tooling::EtsValueToVRegValue<T>(value);
    auto &debugger = runtime->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto err = debugger.SetVariable(thread, previousFrameDepth, regNumber, vregValue);
    if (err) {
        LOG(ERROR, DEBUGGER) << "Failed to set local variable: " << err.value().GetMessage();
        SetRuntimeException(regNumber, executionCtx, ark::ets::tooling::EtsTypeName<T>::NAME);
    }
}

void DebuggerAPISetLocalBoolean(EtsLong regNumber, EtsBoolean value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    DebuggerAPISetLocal<EtsBoolean>(executionCtx, regNumber, value);
}

void DebuggerAPISetLocalByte(EtsLong regNumber, EtsByte value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    DebuggerAPISetLocal<EtsByte>(executionCtx, regNumber, value);
}

void DebuggerAPISetLocalShort(EtsLong regNumber, EtsShort value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    DebuggerAPISetLocal<EtsShort>(executionCtx, regNumber, value);
}

void DebuggerAPISetLocalChar(EtsLong regNumber, EtsChar value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    DebuggerAPISetLocal<EtsChar>(executionCtx, regNumber, value);
}

void DebuggerAPISetLocalInt(EtsLong regNumber, EtsInt value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    DebuggerAPISetLocal<EtsInt>(executionCtx, regNumber, value);
}

void DebuggerAPISetLocalFloat(EtsLong regNumber, EtsFloat value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    DebuggerAPISetLocal<EtsFloat>(executionCtx, regNumber, value);
}

void DebuggerAPISetLocalDouble(EtsLong regNumber, EtsDouble value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    DebuggerAPISetLocal<EtsDouble>(executionCtx, regNumber, value);
}

void DebuggerAPISetLocalLong(EtsLong regNumber, EtsLong value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    DebuggerAPISetLocal<EtsLong>(executionCtx, regNumber, value);
}

void DebuggerAPISetLocalObject(EtsLong regNumber, EtsObject *value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (value == nullptr) {
        DebuggerAPISetLocal<ObjectHeader *>(executionCtx, regNumber, nullptr);
        return;
    }
    [[maybe_unused]] HandleScope<ObjectHeader *> scope(executionCtx->GetMT());
    VMHandle<EtsObject> objHandle(executionCtx->GetMT(), value->GetCoreType());

    DebuggerAPISetLocal<ObjectHeader *>(executionCtx, regNumber, objHandle.GetPtr()->GetCoreType());
}

extern "C" EtsObject *DebuggerAPIGetThis(EtsInt threadId, EtsInt frameDepth)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    auto *runtime = executionCtx->GetPandaVM()->GetRuntime();
    if (UNLIKELY(!runtime->IsDebugMode())) {
        ThrowDebuggerError(executionCtx, "Debugger is not enabled");
        return nullptr;
    }
    if (UNLIKELY(!ManagedThread::IsManagedScope())) {
        ThrowDebuggerError(executionCtx, "Not in managed code scope");
        return nullptr;
    }

    auto *managedThread = executionCtx->GetMT();

    // Pass a GC-updatable handle slot instead of a raw pointer:
    // GetThisVariableByFrame may trigger GC (class initialization during frame
    // walk), and a moving collector would invalidate a raw ObjectHeader pointer
    // written into a stack local.
    HandleScope<ObjectHeader *> scope(managedThread);
    auto *slotPtr = reinterpret_cast<ObjectHeader **>(managedThread->GetTopScope<ObjectHeader *>()->NewHandle(nullptr));

    auto &debugger = runtime->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(managedThread);
    auto err = debugger.GetThisVariableByFrame(thread, static_cast<uint32_t>(frameDepth), slotPtr);
    if (err) {
        LOG(ERROR, DEBUGGER) << "DebuggerAPIGetThis: GetThisVariableByFrame failed for frameDepth=" << frameDepth;
        return nullptr;
    }
    if (*slotPtr == nullptr) {
        LOG(ERROR, DEBUGGER) << "DebuggerAPIGetThis: this is null for frameDepth=" << frameDepth;
        return nullptr;
    }
    return EtsObject::FromCoreType(*slotPtr);
}

struct VariableSearchContext {
    ark::tooling::DebugInterface &debugger;
    ark::tooling::PtThread thread;
    uint32_t frameDepth;
    EtsExecutionContext *executionCtx;
    // Load context of the paused method's class: used to resolve declared
    // type signatures for reference-typed writes.
    ClassLinkerContext *loadContext;
    const panda_file::DebugInfoExtractor &debugInfo;
    const panda_file::File::EntityId &methodId;
    // Number of regular vregs of the frame (PtFrame::GetVRegNum). ETS frames
    // place arguments AFTER the vreg area (GetVReg(i + nregs) in the tooling
    // FillRegisters), so the real vreg of parameter i is vregNum + i -- using
    // the bare parameter index as a register number reads the locals area.
    uint32_t vregNum;
};

struct VariableMetaInfo {
    int32_t regNumber;
    std::string typeSignature;
};

static std::optional<VariableMetaInfo> FindVariableMetaInParameters(const VariableSearchContext &ctx,
                                                                    const std::string &varNameStd)
{
    auto &parameters = ctx.debugInfo.GetParameterInfo(ctx.methodId);
    for (auto i = 0U; i < parameters.size(); i++) {
        if (parameters[i].name == varNameStd) {
            return VariableMetaInfo {static_cast<int32_t>(ctx.vregNum + i), parameters[i].signature};
        }
    }
    return std::nullopt;
}

static std::optional<VariableMetaInfo> FindVariableMetaInLocals(const VariableSearchContext &ctx,
                                                                const std::string &varNameStd, uint32_t frameOffset)
{
    auto &variables = ctx.debugInfo.GetLocalVariableTable(ctx.methodId);
    for (auto &variable : variables) {
        if (variable.name == varNameStd && variable.IsAccessibleAt(frameOffset)) {
            return VariableMetaInfo {variable.regNumber, variable.typeSignature};
        }
    }
    return std::nullopt;
}

// Searches parameters then locals for a variable's metadata (register + signature).
static std::optional<VariableMetaInfo> FindVariableMeta(const VariableSearchContext &ctx, const std::string &varNameStd,
                                                        uint32_t frameOffset)
{
    auto meta = FindVariableMetaInParameters(ctx, varNameStd);
    if (meta) {
        return meta;
    }
    return FindVariableMetaInLocals(ctx, varNameStd, frameOffset);
}

// Returns std::optional<EtsObject*>: nullopt = variable not found,
// nullptr value = variable found but is null, otherwise the object value.
static std::optional<EtsObject *> FindVariableInParameters(const VariableSearchContext &ctx,
                                                           const std::string &varNameStd)
{
    auto &parameters = ctx.debugInfo.GetParameterInfo(ctx.methodId);
    for (auto i = 0U; i < parameters.size(); i++) {
        if (parameters[i].name == varNameStd) {
            ark::tooling::VRegValue vregValue;
            auto err =
                ctx.debugger.GetVariable(ctx.thread, ctx.frameDepth, static_cast<int32_t>(ctx.vregNum + i), &vregValue);
            if (err) {
                ThrowDebuggerError(ctx.executionCtx, "Failed to get variable");
                return nullptr;
            }
            // For reference/union types, a zero vreg means null — return
            // nullptr inside optional (variable found, value is null).
            if (ark::ets::tooling::IsRefSignature(parameters[i].signature) && vregValue.GetValue() == 0) {
                return nullptr;
            }
            return ark::ets::tooling::VRegValueToEtsObject(vregValue, parameters[i].signature, ctx.executionCtx);
        }
    }
    return std::nullopt;
}

// Returns std::optional<EtsObject*>: nullopt = variable not found,
// nullptr value = variable found but is null, otherwise the object value.
static std::optional<EtsObject *> FindVariableInLocals(const VariableSearchContext &ctx, const std::string &varNameStd,
                                                       uint32_t frameOffset)
{
    auto &variables = ctx.debugInfo.GetLocalVariableTable(ctx.methodId);
    for (auto &variable : variables) {
        if (variable.name == varNameStd && variable.IsAccessibleAt(frameOffset)) {
            ark::tooling::VRegValue vregValue;
            auto err = ctx.debugger.GetVariable(ctx.thread, ctx.frameDepth, variable.regNumber, &vregValue);
            if (err) {
                ThrowDebuggerError(ctx.executionCtx, "Failed to get variable");
                return nullptr;
            }
            // For reference/union types, a zero vreg means null — return
            // nullptr inside optional (variable found, value is null).
            if (ark::ets::tooling::IsRefSignature(variable.typeSignature) && vregValue.GetValue() == 0) {
                return nullptr;
            }
            return ark::ets::tooling::VRegValueToEtsObject(vregValue, variable.typeSignature, ctx.executionCtx);
        }
    }
    return std::nullopt;
}

struct ModuleFieldInfo {
    EtsClass *moduleClass;
    EtsField *field;
};

// Returns the class's source-file debug data (the record it was compiled
// from), or nullopt when absent.
static std::optional<panda_file::File::StringData> GetClassSourceFile(Class *klass)
{
    if (klass == nullptr) {
        return std::nullopt;
    }
    const auto *pf = klass->GetPandaFile();
    if (pf == nullptr) {
        return std::nullopt;
    }
    panda_file::ClassDataAccessor cda(*pf, klass->GetFileId());
    auto sfId = cda.GetSourceFileId();
    if (!sfId.has_value()) {
        return std::nullopt;
    }
    return pf->GetStringData(*sfId);
}

// Whether klass is an ETSGLOBAL (per-file module) class of pandaFile.
static bool IsSameFileEtsGlobal(Class *klass, const panda_file::File *pandaFile)
{
    auto *klassFile = klass->GetPandaFile();
    if (klassFile == nullptr || klassFile->GetFilename() != pandaFile->GetFilename()) {
        return false;  // not the frame's module
    }
    return klass->GetName().find("ETSGLOBAL") != std::string::npos;
}

// Finds the ETSGLOBAL class of the frame's own record: within one abc,
// every record (source file) has its own ETSGLOBAL, and classes of one
// record share the source-file debug entry, so matching it pins the frame's
// own module even when sibling records declare the same top-level name.
// Returns nullptr when the frame class has no source info or no matching
// ETSGLOBAL is loaded.
static EtsClass *FindFrameRecordGlobal(ClassLinker *linker, const panda_file::File *pandaFile, Class *frameClass)
{
    auto frameSrc = GetClassSourceFile(frameClass);
    if (!frameSrc.has_value()) {
        return nullptr;
    }
    EtsClass *frameGlobal = nullptr;
    linker->EnumerateClasses([&](Class *klass) -> bool {
        auto klassSrc = GetClassSourceFile(klass);
        if (!IsSameFileEtsGlobal(klass, pandaFile) || !klassSrc.has_value() ||
            !utf::IsEqual(klassSrc->data, frameSrc->data)) {
            return true;
        }
        frameGlobal = EtsClass::FromRuntimeClass(klass);
        return false;  // stop enumeration: the frame's own module
    });
    return frameGlobal;
}

// Finds a top-level (module-level) variable of the frame's own module. Top-level
// declarations compile into static fields of the frame file's per-file module
// class (<pkg>.<file>.ETSGLOBAL) with source-level field names (e.g. a real
// session panel shows entry.src.main.ets.pages.Index.ETSGLOBAL with moduleVar=10).
// The module class is LAZILY loaded by the runtime: before the first ldstatic of
// one of its members it is absent from the ClassLinker, so a top-level variable
// is evaluatable only after its first use -- the same timing semantics as
// class-name resolution and the debugger's Global panel, which shows the module
// class only once loaded and initialized.
// A linked (multi-record) abc contains one ETSGLOBAL per record, all sharing the
// frame's panda file; sibling records may declare the same top-level name, so
// the frame's own record is pinned first (FindFrameRecordGlobal) and only its
// ETSGLOBAL is queried: when it has no such field, the lookup reports not-found
// instead of falling through to sibling records. When the record cannot be
// pinned (no source debug info / ETSGLOBAL not loaded yet), the lookup degrades
// to the ETSGLOBAL-filtered enumeration, which stays correct for single-record
// abc files. ETS scope rules put top-level let/const and a class's static
// fields in separate binding scopes, so a user class may legally declare
// `static x` that collides with a top-level `let x` in the same file: user
// classes are never considered.
static std::optional<ModuleFieldInfo> FindModuleField(const panda_file::File *pandaFile, Class *frameClass,
                                                      const std::string &varName)
{
    auto *linker = Runtime::GetCurrent()->GetClassLinker();
    if (auto *frameGlobal = FindFrameRecordGlobal(linker, pandaFile, frameClass); frameGlobal != nullptr) {
        auto *field = frameGlobal->GetStaticFieldIDByName(varName.c_str());
        if (field != nullptr) {
            return ModuleFieldInfo {frameGlobal, field};
        }
        return std::nullopt;  // the frame's own record has no such field
    }
    std::optional<ModuleFieldInfo> result = std::nullopt;
    linker->EnumerateClasses([&](Class *klass) -> bool {
        if (!IsSameFileEtsGlobal(klass, pandaFile)) {
            return true;
        }
        auto *etsClass = EtsClass::FromRuntimeClass(klass);
        auto *field = etsClass->GetStaticFieldIDByName(varName.c_str());
        if (field != nullptr) {
            result = ModuleFieldInfo {etsClass, field};
            return false;  // stop enumeration
        }
        return true;
    });
    return result;
}

// Widens a boxed numeric value to the box class matching 'target' (only ETS
// widening directions; mirrors ConvertNumericBoxTo in ets_std_core_array.cpp,
// which is not exported). Non-numeric values / unsupported (narrowing)
// conversions are returned unchanged.
static EtsObject *ConvertNumericBoxToClass(EtsExecutionContext *executionCtx, EtsObject *value, EtsClass *target)
{
    if (value == nullptr || target == nullptr || value->GetClass() == target) {
        return value;
    }
    auto *ptypes = PlatformTypes(executionCtx);
    if (target == ptypes->coreDouble || target == ptypes->coreFloat) {
        auto v = GetBoxedNumericValueAsDouble(ptypes, value);
        if (!v.has_value()) {
            return value;
        }
        if (target == ptypes->coreDouble) {
            return EtsBoxPrimitive<EtsDouble>::Create(executionCtx, *v)->AsObject();
        }
        return EtsBoxPrimitive<EtsFloat>::Create(executionCtx, static_cast<float>(*v))->AsObject();
    }
    if (target == ptypes->coreLong || target == ptypes->coreInt) {
        // long/float/double -> int is not an implicit (widening) conversion.
        if (target == ptypes->coreInt && value->GetClass() == ptypes->coreLong) {
            return value;
        }
        auto v = GetBoxedIntegralValue(ptypes, value);
        if (!v.has_value()) {
            return value;
        }
        if (target == ptypes->coreLong) {
            return EtsBoxPrimitive<EtsLong>::Create(executionCtx, *v)->AsObject();
        }
        return EtsBoxPrimitive<EtsInt>::Create(executionCtx, static_cast<int32_t>(*v))->AsObject();
    }
    return value;
}

// Reads a static field as a boxed Object: primitive fields are read via
// GetStaticFieldPrimitive and boxed; reference fields are returned directly.
static EtsObject *ReadStaticFieldValue(EtsExecutionContext *executionCtx, EtsClass *cls, EtsField *field)
{
    switch (field->GetEtsType()) {
        case EtsType::DOUBLE:
            return EtsBoxPrimitive<EtsDouble>::Create(executionCtx, cls->GetStaticFieldPrimitive<EtsDouble>(field))
                ->AsObject();
        case EtsType::FLOAT:
            return EtsBoxPrimitive<EtsFloat>::Create(executionCtx, cls->GetStaticFieldPrimitive<EtsFloat>(field))
                ->AsObject();
        case EtsType::LONG:
            return EtsBoxPrimitive<EtsLong>::Create(executionCtx, cls->GetStaticFieldPrimitive<EtsLong>(field))
                ->AsObject();
        case EtsType::INT:
            return EtsBoxPrimitive<EtsInt>::Create(executionCtx, cls->GetStaticFieldPrimitive<EtsInt>(field))
                ->AsObject();
        case EtsType::BYTE:
            return EtsBoxPrimitive<EtsByte>::Create(executionCtx, cls->GetStaticFieldPrimitive<EtsByte>(field))
                ->AsObject();
        case EtsType::SHORT:
            return EtsBoxPrimitive<EtsShort>::Create(executionCtx, cls->GetStaticFieldPrimitive<EtsShort>(field))
                ->AsObject();
        case EtsType::CHAR:
            return EtsBoxPrimitive<EtsChar>::Create(executionCtx, cls->GetStaticFieldPrimitive<EtsChar>(field))
                ->AsObject();
        case EtsType::BOOLEAN:
            return EtsBoxPrimitive<EtsBoolean>::Create(executionCtx, cls->GetStaticFieldPrimitive<EtsBoolean>(field))
                ->AsObject();
        default:
            return cls->GetStaticFieldObject(field);
    }
}

// Maps a primitive field's EtsType to the corresponding core box class.
static EtsClass *GetCoreBoxClassByEtsType(EtsPlatformTypes const *ptypes, EtsType type)
{
    switch (type) {
        case EtsType::DOUBLE:
            return ptypes->coreDouble;
        case EtsType::FLOAT:
            return ptypes->coreFloat;
        case EtsType::LONG:
            return ptypes->coreLong;
        case EtsType::INT:
            return ptypes->coreInt;
        case EtsType::BYTE:
            return ptypes->coreByte;
        case EtsType::SHORT:
            return ptypes->coreShort;
        case EtsType::CHAR:
            return ptypes->coreChar;
        case EtsType::BOOLEAN:
            return ptypes->coreBoolean;
        default:
            return nullptr;
    }
}

// Writes a boxed value to a static field: primitive fields are numeric-widened
// to the field type (only ETS widening directions) then unboxed; reference
// fields are stored directly. Unsupported types throw a TypeError and skip
// the write (e.g. Int box into a byte field requires an explicit cast in ETS).
template <typename T>
static bool WriteWidenedStaticField(EtsExecutionContext *executionCtx, EtsClass *cls, EtsField *field, EtsObject *value)
{
    auto *ptypes = PlatformTypes(executionCtx);
    auto *targetClass = GetCoreBoxClassByEtsType(ptypes, field->GetEtsType());
    auto *converted = ConvertNumericBoxToClass(executionCtx, value, targetClass);
    if (converted == nullptr || converted->GetClass() != targetClass) {
        ThrowEtsException(executionCtx, ptypes->coreTypeError, "Cannot assign value to module field");
        return false;
    }
    cls->SetStaticFieldPrimitive<T>(field, EtsBoxPrimitive<T>::Unbox(converted));
    return true;
}

static void WriteStaticFieldValue(EtsExecutionContext *executionCtx, EtsClass *cls, EtsField *field, EtsObject *value)
{
    switch (field->GetEtsType()) {
        case EtsType::DOUBLE:
            WriteWidenedStaticField<EtsDouble>(executionCtx, cls, field, value);
            break;
        case EtsType::FLOAT:
            WriteWidenedStaticField<EtsFloat>(executionCtx, cls, field, value);
            break;
        case EtsType::LONG:
            WriteWidenedStaticField<EtsLong>(executionCtx, cls, field, value);
            break;
        case EtsType::INT:
            WriteWidenedStaticField<EtsInt>(executionCtx, cls, field, value);
            break;
        case EtsType::BYTE:
            WriteWidenedStaticField<EtsByte>(executionCtx, cls, field, value);
            break;
        case EtsType::SHORT:
            WriteWidenedStaticField<EtsShort>(executionCtx, cls, field, value);
            break;
        case EtsType::CHAR:
            WriteWidenedStaticField<EtsChar>(executionCtx, cls, field, value);
            break;
        case EtsType::BOOLEAN:
            WriteWidenedStaticField<EtsBoolean>(executionCtx, cls, field, value);
            break;
        default: {
            // Reference field: check assignability before storing. null is
            // allowed for any reference field. SetStaticFieldObject is a raw
            // write with no type check, so without this guard DebuggerAPI.set
            // could place an unrelated object into a typed static/global slot,
            // after which compiled code would observe a type-confused value.
            if (value == nullptr) {
                cls->SetStaticFieldObject(field, nullptr);
                break;
            }
            // Pin the value across GetType: it may trigger class resolution
            // and GC, and a moving collector would invalidate the raw pointer.
            EtsHandleScope handleScope(executionCtx);
            EtsHandle<EtsObject> valueHandle(executionCtx, value);
            auto *fieldType = field->GetType();
            if (fieldType != nullptr && !fieldType->IsAssignableFrom(valueHandle->GetClass())) {
                auto *ptypes = PlatformTypes(executionCtx);
                ThrowEtsException(executionCtx, ptypes->coreTypeError,
                                  "Cannot assign value to module field: type mismatch");
                break;
            }
            cls->SetStaticFieldObject(field, valueHandle.GetPtr());
            break;
        }
    }
}

static std::optional<EtsObject *> FindVariableInFrame(const VariableSearchContext &ctx, const std::string &varNameStd,
                                                      uint32_t frameOffset)
{
    auto paramResult = FindVariableInParameters(ctx, varNameStd);
    if (paramResult.has_value()) {
        auto *obj = *paramResult;
        LOG(INFO, DEBUGGER) << "DebuggerAPIGet: found '" << varNameStd << "' in parameters, obj class="
                            << (obj != nullptr && obj->GetCoreType() != nullptr
                                    ? obj->GetCoreType()->ClassAddr<Class>()->GetName()
                                    : "null");
        return obj;
    }
    auto localResult = FindVariableInLocals(ctx, varNameStd, frameOffset);
    if (localResult.has_value()) {
        auto *obj = *localResult;
        LOG(INFO, DEBUGGER) << "DebuggerAPIGet: found '" << varNameStd << "' in locals, obj class="
                            << (obj != nullptr && obj->GetCoreType() != nullptr
                                    ? obj->GetCoreType()->ClassAddr<Class>()->GetName()
                                    : "null");
        return obj;
    }
    return std::nullopt;
}

// Module-level fallback: top-level const/let of the frame's own module
// (static fields of the module class, source-level names). Returns nullptr on
// a miss AND for a null field value (preserving the historical fall-through
// to the later lookup stages).
// Resolves a top-level (module) variable to its current value. Returns
// nullopt when no such field exists; the contained value may be null (a
// nullable module variable currently holding null -- the write path stores
// null), which must not be treated as a lookup miss.
static std::optional<EtsObject *> ResolveModuleField(EtsExecutionContext *executionCtx,
                                                     const panda_file::File *pandaFile, Class *frameClass,
                                                     const std::string &varNameStd)
{
    auto moduleField = FindModuleField(pandaFile, frameClass, varNameStd);
    if (!moduleField.has_value()) {
        return std::nullopt;
    }
    auto *value = ReadStaticFieldValue(executionCtx, moduleField->moduleClass, moduleField->field);
    if (value == nullptr) {
        return nullptr;  // found, currently null
    }
    VMHandle<ObjectHeader> valueHandle(executionCtx->GetMT(), value->GetCoreType());
    return EtsObject::FromCoreType(valueHandle.GetPtr());
}

// Class-name fallback: resolve as a source-level class name and return the
// Class object. Only classes that are ALREADY initialized are resolved.
static EtsClass *ResolveClassName(const panda_file::File *pandaFile, EtsExecutionContext *executionCtx,
                                  const std::string &varNameStd, ClassLinkerContext *loadContext)
{
    auto *cls = ResolveClassByName(pandaFile, executionCtx, varNameStd, loadContext);
    if (cls != nullptr && cls->GetRuntimeClass()->IsInitialized()) {
        return cls;
    }
    return nullptr;
}

static bool CheckDebugPreconditions(EtsExecutionContext *executionCtx, EtsString *name, const char *nullNameMsg)
{
    auto *runtime = executionCtx->GetPandaVM()->GetRuntime();
    if (UNLIKELY(!runtime->IsDebugMode())) {
        ThrowDebuggerError(executionCtx, "Debugger is not enabled");
        return false;
    }
    if (UNLIKELY(name == nullptr)) {
        ThrowDebuggerError(executionCtx, nullNameMsg);
        return false;
    }
    if (UNLIKELY(!ManagedThread::IsManagedScope())) {
        ThrowDebuggerError(executionCtx, "Not in managed code scope");
        return false;
    }
    return true;
}

// ===== Session variable store (evaluate let/const declarations) =====
// Values live in the DebuggerAPI.sessionStore static field -- an Object[] of
// [name, value, readonlyMark] triples. A static field is a permanent GC root,
// so no handle lifecycle is needed for cross-invocation survival (per-thread
// handle storages die with their threads and are not usable here). The static
// field is re-read on every access because the array object may move; every
// element access except growth is allocation-free, and the growth path keeps
// all live references handle-scoped across its single allocation.
// readonlyMark: null (let) / the name string itself (const) -- no boxing
// allocation under the lock. The store is process-lifetime (v1): variables
// survive detach/reattach within one process, but since values are
// GC-managed, an uncleared store can neither dangle nor leak.
namespace {
os::memory::Mutex g_sessionStoreMutex;  // NOLINT(fuchsia-statically-constructed-objects)

constexpr uint32_t SESSION_TRIPLE_SIZE = 3;
// Session store triple layout: [name, value, readonly-mark]
constexpr uint32_t SESSION_SLOT_NAME = 0;
constexpr uint32_t SESSION_SLOT_VALUE = 1;
constexpr uint32_t SESSION_SLOT_READONLY = 2;

EtsClass *GetDebuggerApiEtsClass()
{
    auto *linker = Runtime::GetCurrent()->GetClassLinker();
    auto *extension = linker->GetExtension(panda_file::SourceLang::ETS);
    if (extension == nullptr) {
        return nullptr;
    }
    static constexpr std::string_view DESCRIPTOR = "Lstd/debug/DebuggerAPI;";
    auto *runtimeClass = extension->FindLoadedClass(reinterpret_cast<const uint8_t *>(DESCRIPTOR.data()));
    if (runtimeClass == nullptr) {
        return nullptr;
    }
    return EtsClass::FromRuntimeClass(runtimeClass);
}

EtsField *GetSessionStoreField(EtsClass *etsClass)
{
    return etsClass->GetStaticFieldIDByName("sessionStore");
}

struct SessionVariableInfo {
    EtsObjectArray *array;
    uint32_t valueSlot;
    bool readonly;
    EtsObject *value;  // may be null: `let a = null`
};

// Looks up a session variable by name. No allocations (caller may hold the
// mutex); returns nullopt when the store or the name is absent.
std::optional<SessionVariableInfo> FindSessionVariable(EtsClass *etsClass, EtsField *field, const char *name)
{
    auto *arrObj = etsClass->GetStaticFieldObject(field);
    // ETS null is the runtime null singleton, not C++ nullptr
    if (ark::ets::tooling::IsNullOrNullValue(arrObj)) {
        return std::nullopt;
    }
    auto *array = EtsObjectArray::FromEtsObject(arrObj);
    uint32_t length = array->GetLength();
    for (uint32_t base = 0; base + SESSION_TRIPLE_SIZE <= length; base += SESSION_TRIPLE_SIZE) {
        auto *nameObj = array->Get(base);
        if (nameObj != nullptr && nameObj->IsStringClass() && EtsString::FromEtsObject(nameObj)->IsEqual(name)) {
            return SessionVariableInfo {array, base + 1, array->Get(base + 2) != nullptr, array->Get(base + 1)};
        }
    }
    return std::nullopt;
}

// The name/value/readonly inputs of a session-variable definition. All
// object references are handle-protected across the store growth allocation.
struct SessionVariableDraft {
    EtsHandle<EtsString> &nameHandle;
    EtsHandle<EtsObject> &valueHandle;
    EtsHandle<EtsObject> &nameAsObjHandle;
    EtsBoolean readonly;
};

// Identity of the session store: the owner class and its static field.
struct SessionStoreLocation {
    EtsExecutionContext *executionCtx;
    EtsClass *etsClass;
    EtsField *field;
};

// Snapshot of the published session store, read WITHOUT the lock for the
// optimistic growth protocol. The array is handle-protected: other threads
// may run GC between the phases.
struct SessionStoreSnapshot {
    EtsHandle<EtsObject> array;
    uint32_t length {0};
    uint32_t appendBase {0};
};

// First free triple of the store (the store is append-only, so occupied
// triples form a prefix). No allocations.
uint32_t FindAppendBase(EtsObjectArray *array)
{
    uint32_t appendBase = 0;
    if (array == nullptr) {
        return 0;
    }
    uint32_t length = array->GetLength();
    while (appendBase + SESSION_TRIPLE_SIZE <= length && array->Get(appendBase) != nullptr) {
        appendBase += SESSION_TRIPLE_SIZE;
    }
    return appendBase;
}

// Reads the published store state without the lock (no allocations).
SessionStoreSnapshot SnapshotSessionStore(EtsExecutionContext *executionCtx, EtsClass *etsClass, EtsField *field)
{
    auto *arrObj = etsClass->GetStaticFieldObject(field);
    // ETS null is the runtime null singleton, not C++ nullptr
    if (ark::ets::tooling::IsNullOrNullValue(arrObj)) {
        return SessionStoreSnapshot {EtsHandle<EtsObject>(executionCtx, nullptr), 0, 0};
    }
    auto *array = EtsObjectArray::FromEtsObject(arrObj);
    return SessionStoreSnapshot {EtsHandle<EtsObject>(executionCtx, arrObj), static_cast<uint32_t>(array->GetLength()),
                                 FindAppendBase(array)};
}

// Pre-allocates a bigger store array OUTSIDE the store mutex: this is the
// only managed allocation of the define path. Allocating while holding the
// OS mutex could deadlock stop-the-world GC — the allocator blocks in
// WaitForGCInManaged waiting for a mutator that is itself blocked on the
// mutex. Returns an empty handle on allocation failure.
EtsHandle<EtsObject> AllocateSessionArray(EtsExecutionContext *executionCtx, uint32_t minLen, uint32_t currentLen)
{
    uint32_t newLen = std::max(minLen, currentLen * 2U);
    auto *newArray = EtsObjectArray::Create(PlatformTypes(executionCtx)->coreObject, newLen);
    if (newArray == nullptr) {
        return EtsHandle<EtsObject>(executionCtx, nullptr);
    }
    return EtsHandle<EtsObject>(executionCtx, EtsObject::FromCoreType(newArray->GetCoreType()));
}

// Appends one [name, value, readonly-mark] triple. No allocations.
void WriteSessionTriple(EtsObjectArray *array, uint32_t base, const SessionVariableDraft &draft)
{
    array->Set(base + SESSION_SLOT_NAME, draft.nameAsObjHandle.GetPtr());
    array->Set(base + SESSION_SLOT_VALUE, draft.valueHandle.GetPtr());
    array->Set(base + SESSION_SLOT_READONLY, draft.readonly != 0 ? draft.nameAsObjHandle.GetPtr() : nullptr);
}

// Publishes the definition under the store mutex. NO managed allocation is
// allowed here (see AllocateSessionVariable): only reference copies. The
// snapshot is revalidated (another evaluator may have defined or grown the
// store concurrently); returns false to retry with a fresh snapshot, true
// when the variable is written (including a concurrent redefine that won
// the race).
bool PublishSessionVariable(const SessionStoreLocation &loc, const SessionVariableDraft &draft,
                            const SessionStoreSnapshot &snapshot, EtsHandle<EtsObject> &prealloc)
{
    os::memory::LockHolder lock(g_sessionStoreMutex);
    std::string varNameStd(draft.nameHandle->GetMutf8());
    auto found = FindSessionVariable(loc.etsClass, loc.field, varNameStd.c_str());
    if (found.has_value()) {
        // redefine: overwrite value and readonly mark in place
        found->array->Set(found->valueSlot, draft.valueHandle.GetPtr());
        found->array->Set(found->valueSlot + (SESSION_SLOT_READONLY - SESSION_SLOT_VALUE),
                          draft.readonly != 0 ? draft.nameAsObjHandle.GetPtr() : nullptr);
        return true;
    }
    // Revalidate: the published array must still be the snapshotted one (a
    // concurrent grow replaces it) and the store must not have appeared
    // between the phases. GC movement is handled by the handles.
    auto *arrObj = loc.etsClass->GetStaticFieldObject(loc.field);
    bool storeEmpty = ark::ets::tooling::IsNullOrNullValue(arrObj);
    if (!storeEmpty && arrObj != snapshot.array.GetPtr()) {
        return false;  // grown or replaced concurrently: retry
    }
    if (storeEmpty && snapshot.array.GetPtr() != nullptr) {
        return false;  // created concurrently between the phases: retry
    }
    auto *current = storeEmpty ? nullptr : EtsObjectArray::FromEtsObject(arrObj);
    // Recompute the append base: a concurrent define may have advanced it.
    uint32_t appendBase = FindAppendBase(current);
    if (current != nullptr && appendBase + SESSION_TRIPLE_SIZE <= current->GetLength()) {
        WriteSessionTriple(current, appendBase, draft);
        return true;
    }
    auto *target = EtsObjectArray::FromEtsObject(prealloc.GetPtr());
    if (target == nullptr || appendBase + SESSION_TRIPLE_SIZE > target->GetLength()) {
        return false;  // no pre-allocation, or it does not cover the new base: retry
    }
    for (uint32_t i = 0; i < snapshot.length; i++) {
        target->Set(i, current->Get(i));
    }
    WriteSessionTriple(target, appendBase, draft);
    loc.etsClass->SetStaticFieldObject(loc.field, prealloc.GetPtr());
    return true;
}

// Defines or redefines a session variable. The growth allocation happens
// OUTSIDE the store mutex (see AllocateSessionArray); the publish phase
// under the lock only revalidates the snapshot and copies references.
// Returns false when the backing array could not be allocated (the caller
// reports the error outside the lock).
// Bound for the optimistic-retry loop of DefineSessionVariable: each retry
// requires another evaluator to modify the store between the snapshot and
// the publish phase, so more than a couple of rounds means sustained
// contention; give up and report failure (the caller reports it).
constexpr uint32_t SESSION_DEFINE_MAX_ATTEMPTS = 8;

bool DefineSessionVariable(EtsExecutionContext *executionCtx, EtsClass *etsClass, EtsField *field,
                           const SessionVariableDraft &draft)
{
    SessionStoreLocation loc {executionCtx, etsClass, field};
    EtsHandleScope handleScope(executionCtx);
    for (uint32_t attempt = 0; attempt < SESSION_DEFINE_MAX_ATTEMPTS; attempt++) {
        SessionStoreSnapshot snapshot = SnapshotSessionStore(executionCtx, etsClass, field);
        EtsHandle<EtsObject> prealloc(executionCtx, nullptr);
        if (snapshot.appendBase + SESSION_TRIPLE_SIZE > snapshot.length) {
            prealloc = AllocateSessionArray(executionCtx, snapshot.appendBase + SESSION_TRIPLE_SIZE, snapshot.length);
            if (prealloc.GetPtr() == nullptr) {
                return false;
            }
        }
        if (PublishSessionVariable(loc, draft, snapshot, prealloc)) {
            return true;
        }
        // The store changed concurrently: retry with a fresh snapshot (the
        // dropped pre-allocation is simply collected).
    }
    return false;  // sustained concurrent modification: the caller reports the failure
}
}  // namespace

// Registers (or redefines) a session variable from an evaluate let/const
// declaration. thread/frame are accepted for signature consistency only: the
// session store is frame-independent, matching the name-resolution priority
// model (frame variables and module fields shadow session variables).
extern "C" void DebuggerAPIDefineVariable(EtsInt threadId, EtsInt frameDepth, EtsString *name, EtsObject *value,
                                          EtsBoolean readonly)
{
    (void)threadId;
    (void)frameDepth;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (!CheckDebugPreconditions(executionCtx, name, "Variable name is null")) {
        return;
    }
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> nameHandle(executionCtx, name);
    // value may be null: `let a = null` stores a null slot (Any semantics)
    EtsHandle<EtsObject> valueHandle(executionCtx, value);
    // EtsString and EtsObject are sibling reinterpret types; the stored name
    // slot (and the const readonly mark, which reuses the name object) needs
    // the EtsObject view, kept handle-scoped across the growth allocation
    EtsHandle<EtsObject> nameAsObjHandle(executionCtx, EtsObject::FromCoreType(nameHandle->GetCoreType()));
    auto *etsClass = GetDebuggerApiEtsClass();
    if (etsClass == nullptr) {
        ThrowDebuggerError(executionCtx, "std.debug.DebuggerAPI is not loaded");
        return;
    }
    auto *field = GetSessionStoreField(etsClass);
    if (field == nullptr) {
        ThrowDebuggerError(executionCtx, "Session store field is not available");
        return;
    }
    if (!DefineSessionVariable(executionCtx, etsClass, field,
                               SessionVariableDraft {nameHandle, valueHandle, nameAsObjHandle, readonly})) {
        // outside the lock: error construction allocates
        ThrowDebuggerError(executionCtx, "Failed to allocate the session store");
    }
}

// Session variables (evaluate let/const declarations): after the frame
// and module lookups, before the class-name fallback. Lookup only, no
// allocations, so the lock is safe. Returns nullopt when the name is
// absent; the contained value may be null (`let a = null`).
static std::optional<EtsObject *> FindSessionValueByName(const std::string &varNameStd)
{
    os::memory::LockHolder lock(g_sessionStoreMutex);
    auto *etsClass = GetDebuggerApiEtsClass();
    if (etsClass == nullptr) {
        return std::nullopt;
    }
    auto *storeField = GetSessionStoreField(etsClass);
    if (storeField == nullptr) {
        return std::nullopt;
    }
    auto sessionVar = FindSessionVariable(etsClass, storeField, varNameStd.c_str());
    if (!sessionVar.has_value()) {
        return std::nullopt;
    }
    return sessionVar->value;
}

// Class-name fallback: an unresolved identifier may name a class. Returns a
// VM-handle-protected class object, or nullptr.
static EtsObject *ResolveClassValueByName(const panda_file::File *pandaFile, EtsExecutionContext *executionCtx,
                                          const std::string &varNameStd, Method *method)
{
    auto *cls = ResolveClassName(pandaFile, executionCtx, varNameStd, method->GetClass()->GetLoadContext());
    if (cls == nullptr) {
        return nullptr;
    }
    VMHandle<ObjectHeader> classHandle(executionCtx->GetMT(), cls->AsObject()->GetCoreType());
    return EtsObject::FromCoreType(classHandle.GetPtr());
}

// Reports a lookup failure for get()-style resolution: only throws when the
// caller requested missing-name errors (getOptional passes false).
static void ReportMissing(EtsExecutionContext *executionCtx, bool throwOnMissing, const std::string &msg)
{
    if (throwOnMissing) {
        ThrowDebuggerError(executionCtx, msg);
    }
}

// The final miss of get(): ReferenceError "name is not defined".
static void ReportReferenceError(EtsExecutionContext *executionCtx, bool throwOnMissing, const std::string &varNameStd)
{
    if (throwOnMissing) {
        ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreReferenceError,
                          (varNameStd + " is not defined").c_str());
    }
}

// Shared variable resolution for get() and getOptional(): frame vars → module
// fields → session store → class name. When throwOnMissing is true, throws
// ReferenceError on a complete miss; otherwise returns nullptr.
static EtsObject *ResolveVariableByName(EtsExecutionContext *executionCtx, EtsInt frameDepth, EtsString *name,
                                        bool throwOnMissing)
{
    if (!CheckDebugPreconditions(executionCtx, name, "Variable name is null")) {
        return nullptr;
    }
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> nameHandle(executionCtx, name);
    std::string varNameStd(nameHandle->GetMutf8());
    auto &debugger = executionCtx->GetPandaVM()->GetRuntime()->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto frameResult = debugger.GetFrameByThread(thread, static_cast<uint32_t>(frameDepth));
    if (!frameResult) {
        ReportMissing(executionCtx, throwOnMissing, "Frame not found");
        return nullptr;
    }
    auto method = (*frameResult)->GetMethod();
    auto *pandaFile = method->GetPandaFile();
    if (UNLIKELY(pandaFile == nullptr)) {
        ReportMissing(executionCtx, throwOnMissing, "No panda file for method");
        return nullptr;
    }
    panda_file::DebugInfoExtractor debugInfo(pandaFile);
    auto methodId = method->GetFileId();
    VariableSearchContext ctx {debugger,
                               thread,
                               static_cast<uint32_t>(frameDepth),
                               executionCtx,
                               method->GetClass()->GetLoadContext(),
                               debugInfo,
                               methodId,
                               static_cast<uint32_t>((*frameResult)->GetVRegNum())};
    if (auto frameVar = FindVariableInFrame(ctx, varNameStd, (*frameResult)->GetBytecodeOffset());
        frameVar.has_value()) {
        return *frameVar;
    }
    if (auto moduleValue = ResolveModuleField(executionCtx, pandaFile, method->GetClass(), varNameStd);
        moduleValue.has_value()) {
        return *moduleValue;  // may be null: the field exists and holds null
    }
    if (auto sessionValue = FindSessionValueByName(varNameStd); sessionValue.has_value()) {
        return *sessionValue;  // may be null: `let a = null`
    }
    if (auto *classValue = ResolveClassValueByName(pandaFile, executionCtx, varNameStd, method);
        classValue != nullptr) {
        return classValue;
    }
    ReportReferenceError(executionCtx, throwOnMissing, varNameStd);
    return nullptr;
}

extern "C" EtsObject *DebuggerAPIGet(EtsInt threadId, EtsInt frameDepth, EtsString *name)
{
    (void)threadId;
    return ResolveVariableByName(EtsExecutionContext::GetCurrent(), frameDepth, name, true);
}

// getOptional: same as get() but returns null for missing variables instead
// of throwing ReferenceError. Used by optional chaining (a?.b) so that the
// null return value short-circuits subsequent .getField() / .call() correctly.
extern "C" EtsObject *DebuggerAPIGetOptional(EtsInt threadId, EtsInt frameDepth, EtsString *name)
{
    (void)threadId;
    return ResolveVariableByName(EtsExecutionContext::GetCurrent(), frameDepth, name, false);
}

// Outcome of a scoped write: NOT_FOUND = the binding does not exist in this
// scope (fall through to the next one), WRITTEN = the value was stored,
// FAILED = the binding exists but the write was rejected (an exception is
// pending; the scope lookup must stop, never falling through to an outer
// same-named binding).
enum class ScopedWriteResult { NOT_FOUND, WRITTEN, FAILED };

static ScopedWriteResult SetModuleField(EtsExecutionContext *executionCtx, const panda_file::File *pandaFile,
                                        Class *frameClass, const std::string &varNameStd,
                                        EtsHandle<EtsObject> &valueHandle)
{
    auto moduleField = FindModuleField(pandaFile, frameClass, varNameStd);
    if (!moduleField.has_value()) {
        return ScopedWriteResult::NOT_FOUND;
    }
    if (moduleField->field->IsReadonly()) {
        ThrowDebuggerError(executionCtx, "Cannot assign to constant '" + varNameStd + "'");
        return ScopedWriteResult::FAILED;
    }
    WriteStaticFieldValue(executionCtx, moduleField->moduleClass, moduleField->field, valueHandle.GetPtr());
    // WriteStaticFieldValue reports a reference-type mismatch via a pending
    // TypeError; a clear pending state after the write means rejection.
    if (UNLIKELY(executionCtx->GetMT()->HasPendingException())) {
        return ScopedWriteResult::FAILED;
    }
    return ScopedWriteResult::WRITTEN;
}

static ScopedWriteResult SetFrameVariable(const VariableSearchContext &ctx, const std::string &varNameStd,
                                          uint32_t frameOffset, EtsHandle<EtsObject> &valueHandle)
{
    auto meta = FindVariableMeta(ctx, varNameStd, frameOffset);
    if (!meta) {
        return ScopedWriteResult::NOT_FOUND;
    }
    auto vregValue =
        ConvertAndUnboxToVRegValue(ctx.executionCtx, ctx.loadContext, valueHandle.GetPtr(), meta->typeSignature);
    if (!vregValue.has_value()) {
        return ScopedWriteResult::FAILED;  // TypeError already thrown
    }
    auto err = ctx.debugger.SetVariable(ctx.thread, ctx.frameDepth, meta->regNumber, *vregValue);
    if (err) {
        ThrowDebuggerError(ctx.executionCtx,
                           "Failed to set variable '" + varNameStd + "': " + err.value().GetMessage());
        return ScopedWriteResult::FAILED;
    }
    return ScopedWriteResult::WRITTEN;
}

// Writes a session variable under the store mutex (set never creates one).
enum class SessionWriteResult { NOT_FOUND, WRITTEN, READONLY };

static SessionWriteResult TryWriteSessionVariable(const std::string &varNameStd, EtsHandle<EtsObject> &valueHandle)
{
    os::memory::LockHolder lock(g_sessionStoreMutex);
    auto *etsClass = GetDebuggerApiEtsClass();
    if (etsClass == nullptr) {
        return SessionWriteResult::NOT_FOUND;
    }
    auto *storeField = GetSessionStoreField(etsClass);
    if (storeField == nullptr) {
        return SessionWriteResult::NOT_FOUND;
    }
    auto sessionVar = FindSessionVariable(etsClass, storeField, varNameStd.c_str());
    if (!sessionVar.has_value()) {
        return SessionWriteResult::NOT_FOUND;
    }
    if (sessionVar->readonly) {
        return SessionWriteResult::READONLY;
    }
    sessionVar->array->Set(sessionVar->valueSlot, valueHandle.GetPtr());
    return SessionWriteResult::WRITTEN;
}

// Session variables (evaluate let/const declarations): the last write
// target of set(), after the frame variable and the module field -- the same
// priority as the read side. Only existing names are written (set never
// creates a session variable); a const declaration rejects the write.
static void FinishSessionVariableWrite(EtsExecutionContext *executionCtx, const std::string &varNameStd,
                                       EtsHandle<EtsObject> &valueHandle)
{
    auto sessionResult = TryWriteSessionVariable(varNameStd, valueHandle);
    if (sessionResult == SessionWriteResult::READONLY) {
        // outside the lock: error construction allocates; text aligned with
        // the module-field const rejection
        ThrowDebuggerError(executionCtx, "Cannot assign to constant '" + varNameStd + "'");
        return;
    }
    if (sessionResult == SessionWriteResult::WRITTEN) {
        return;
    }
    ThrowDebuggerError(executionCtx, "Variable, module or class '" + varNameStd + "' not found");
}

extern "C" void DebuggerAPISet(EtsInt threadId, EtsInt frameDepth, EtsString *name, EtsObject *value)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (!CheckDebugPreconditions(executionCtx, name, "Variable name is null")) {
        return;
    }
    if (UNLIKELY(value == nullptr)) {
        ThrowDebuggerError(executionCtx, "Value is null");
        return;
    }
    auto *runtime = executionCtx->GetPandaVM()->GetRuntime();
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> nameHandle(executionCtx, name);
    EtsHandle<EtsObject> valueHandle(executionCtx, value);
    std::string varNameStd(nameHandle->GetMutf8());
    auto &debugger = runtime->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto frameResult = debugger.GetFrameByThread(thread, static_cast<uint32_t>(frameDepth));
    if (!frameResult) {
        ThrowDebuggerError(executionCtx, "Frame not found");
        return;
    }
    auto method = (*frameResult)->GetMethod();
    auto *pandaFile = method->GetPandaFile();
    if (UNLIKELY(pandaFile == nullptr)) {
        ThrowDebuggerError(executionCtx, "No panda file for method");
        return;
    }
    panda_file::DebugInfoExtractor debugInfo(pandaFile);
    auto methodId = method->GetFileId();
    VariableSearchContext ctx {debugger,
                               thread,
                               static_cast<uint32_t>(frameDepth),
                               executionCtx,
                               method->GetClass()->GetLoadContext(),
                               debugInfo,
                               methodId,
                               static_cast<uint32_t>((*frameResult)->GetVRegNum())};
    auto frameOffset = (*frameResult)->GetBytecodeOffset();
    if (SetFrameVariable(ctx, varNameStd, frameOffset, valueHandle) != ScopedWriteResult::NOT_FOUND) {
        return;  // found in the frame: written, or rejected with an exception pending
    }
    if (SetModuleField(executionCtx, pandaFile, method->GetClass(), varNameStd, valueHandle) !=
        ScopedWriteResult::NOT_FOUND) {
        return;  // found as a module field: written, or rejected with an exception pending
    }
    FinishSessionVariableWrite(executionCtx, varNameStd, valueHandle);
}

extern "C" EtsObject *DebuggerAPIGetLocalValue(EtsInt threadId, EtsInt frameDepth, EtsLong regNumber,
                                               EtsString *typeSignature)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    auto *runtime = executionCtx->GetPandaVM()->GetRuntime();
    if (UNLIKELY(!runtime->IsDebugMode())) {
        ThrowDebuggerError(executionCtx, "Debugger is not enabled");
        return nullptr;
    }
    if (UNLIKELY(regNumber < 0)) {
        SetRuntimeException(regNumber, executionCtx, "Object");
        return nullptr;
    }
    if (UNLIKELY(!ManagedThread::IsManagedScope())) {
        ThrowDebuggerError(executionCtx, "Not in managed code scope");
        return nullptr;
    }

    std::string sig = (typeSignature != nullptr) ? std::string(typeSignature->GetMutf8()) : std::string();

    ark::tooling::VRegValue vregValue;
    auto &debugger = runtime->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto err =
        debugger.GetVariable(thread, static_cast<uint32_t>(frameDepth), static_cast<int32_t>(regNumber), &vregValue);
    if (err) {
        ThrowDebuggerError(executionCtx, "Failed to get local variable: " + err.value().GetMessage());
        return nullptr;
    }

    return ark::ets::tooling::VRegValueToEtsObject(vregValue, sig, executionCtx);
}

// Load context of the frame's method class (the paused method's context),
// used to resolve declared type signatures. Returns nullptr when the frame
// cannot be resolved.
static ClassLinkerContext *GetFrameLoadContext(ark::tooling::DebugInterface &debugger, ark::tooling::PtThread thread,
                                               uint32_t frameDepth)
{
    auto frameResult = debugger.GetFrameByThread(thread, frameDepth);
    if (!frameResult) {
        return nullptr;
    }
    return (*frameResult)->GetMethod()->GetClass()->GetLoadContext();
}

extern "C" void DebuggerAPISetLocalValue(EtsInt threadId, EtsInt frameDepth, EtsLong regNumber,
                                         EtsString *typeSignature, EtsObject *value)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    auto *runtime = executionCtx->GetPandaVM()->GetRuntime();
    if (UNLIKELY(!runtime->IsDebugMode())) {
        ThrowDebuggerError(executionCtx, "Debugger is not enabled");
        return;
    }
    if (UNLIKELY(regNumber < 0)) {
        SetRuntimeException(regNumber, executionCtx, "Object");
        return;
    }
    if (UNLIKELY(value == nullptr)) {
        ThrowDebuggerError(executionCtx, "Value is null");
        return;
    }

    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> sigHandle(executionCtx, typeSignature);
    EtsHandle<EtsObject> valueHandle(executionCtx, value);
    if (UNLIKELY(sigHandle.GetPtr() == nullptr)) {
        ThrowDebuggerError(executionCtx, "Type signature is null");
        return;
    }
    std::string sig(sigHandle->GetMutf8());
    if (UNLIKELY(sig.empty())) {
        ThrowDebuggerError(executionCtx, "Type signature is empty");
        return;
    }
    auto &debugger = runtime->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto *loadContext = GetFrameLoadContext(debugger, thread, static_cast<uint32_t>(frameDepth));
    if (UNLIKELY(loadContext == nullptr)) {
        ThrowDebuggerError(executionCtx, "Frame not found");
        return;
    }
    auto vregValue = ConvertAndUnboxToVRegValue(executionCtx, loadContext, valueHandle.GetPtr(), sig);
    if (!vregValue.has_value()) {
        // TypeError already thrown; do not write anything.
        return;
    }
    auto err =
        debugger.SetVariable(thread, static_cast<uint32_t>(frameDepth), static_cast<int32_t>(regNumber), *vregValue);
    if (err) {
        ThrowDebuggerError(executionCtx, "Failed to set local variable: " + err.value().GetMessage());
    }
}

extern "C" EtsObject *DebuggerAPIGetGlobalVariable(EtsString *name)
{
    (void)name;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    auto *pt = PlatformTypes(executionCtx);
    if (pt != nullptr && pt->escompatError != nullptr) {
        ThrowEtsException(executionCtx, pt->escompatError, "Global variable access not yet implemented");
    }
    return nullptr;
}

extern "C" void DebuggerAPISetGlobalVariable(EtsString *name, EtsObject *value)
{
    (void)name;
    (void)value;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    auto *pt = PlatformTypes(executionCtx);
    if (pt != nullptr && pt->escompatError != nullptr) {
        ThrowEtsException(executionCtx, pt->escompatError, "Global variable access not yet implemented");
    }
}

static void WritePrimitiveToStdCoreArray(EtsExecutionContext *executionCtx, EtsHandle<EtsObject> &arrHandle,
                                         ark::tooling::VRegValue vreg, const std::string &elementSig, EtsInt index)
{
    auto *boxed = ark::ets::tooling::BoxVRegValue(vreg, elementSig[0], executionCtx);
    if (boxed == nullptr) {
        return;
    }
    EtsStdCoreArray::FromEtsObject(arrHandle.GetPtr())->StdCoreArraySet(index, boxed);
}

// Bounds check for native-array (FixedArray/ValueArray) element access,
// mirroring the interpreter's CheckLoadArrayOp/CheckStoreArrayOp and
// EtsStdCoreArray::IsOutOfBounds: throws std.core.RangeError and returns
// false on an out-of-bounds index.
static bool CheckNativeArrayIndex(EtsExecutionContext *executionCtx, EtsHandle<EtsObject> &arrHandle, EtsInt index)
{
    auto length = static_cast<EtsInt>(EtsArray::FromEtsObject(arrHandle.GetPtr())->GetLength());
    if (UNLIKELY(index < 0 || index >= length)) {
        ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreRangeError, "Out of bounds");
        return false;
    }
    return true;
}

// Component signature of a native array from its runtime class descriptor
// (JVM-style: "[I", "[Lstd/core/Int;", "[[I"). Returns 0 for a reference
// component. NOTE: the EtsClass identity of the component must NOT be used
// for this dispatch: in this toolchain FixedArray<T> is a boxed object array
// (e.g. FixedArray<int> is std.core.Int[] with starr.obj storage) while only
// ValueArray<T> is a primitive array — the component class of
// FixedArray<int> IS coreInt, yet the storage is a reference. The descriptor
// reflects the actual storage faithfully.
static char GetComponentSigFromArrayDescriptor(EtsObject *arr)
{
    auto *runtimeClass = arr->GetCoreType()->ClassAddr<Class>();
    std::string_view descriptor = utf::Mutf8AsCString(runtimeClass->GetDescriptor());
    if (descriptor[0] != '[' || descriptor[1] == 'L' || descriptor[1] == '[') {
        return 0;  // reference component
    }
    return descriptor[1];
}

static void WritePrimitiveToFixedArray(EtsExecutionContext *executionCtx, EtsHandle<EtsObject> &arrHandle,
                                       ark::tooling::VRegValue vreg, EtsInt index)
{
    if (UNLIKELY(!CheckNativeArrayIndex(executionCtx, arrHandle, index))) {
        return;
    }
    auto *rawArray = EtsArray::FromEtsObject(arrHandle.GetPtr());
    // NOLINTNEXTLINE(readability-identifier-naming)
    size_t elemSize = rawArray->GetElementSize();
    auto *data = rawArray->template GetData<uint8_t>();
    int64_t rawValue = vreg.GetValue();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (memcpy_s(data + static_cast<size_t>(index) * elemSize, elemSize, &rawValue, elemSize) != EOK) {
        return;
    }
}

// Writes a reference element; returns false if the array type is unsupported.
// Whether writeValue may be stored into a reference array whose component
// class is componentType: null (allowed for any reference array), or an
// instance of the component type — mirroring the interpreter's reference
// array store check. Returns false and raises ArrayStoreError otherwise.
// A null componentType (not recorded) skips the check.
static bool CheckReferenceArrayStore(EtsHandle<EtsObject> &arrHandle, EtsObject *writeValue)
{
    if (writeValue == nullptr) {
        return true;  // null is storable into any reference array
    }
    auto *componentType = arrHandle.GetPtr()->GetClass()->GetComponentType();
    if (componentType == nullptr || componentType->IsAssignableFrom(writeValue->GetClass())) {
        return true;
    }
    ark::ThrowArrayStoreException(arrHandle.GetPtr()->GetClass()->GetRuntimeClass(),
                                  writeValue->GetClass()->GetRuntimeClass());
    return false;
}

static bool WriteReferenceArrayElement(EtsExecutionContext *executionCtx, EtsHandle<EtsObject> &arrHandle,
                                       EtsObject *writeValue, EtsInt index)
{
    if (EtsStdCoreArray::IsExactlyStdCoreArray(arrHandle.GetPtr(), executionCtx)) {
        EtsStdCoreArray::FromEtsObject(arrHandle.GetPtr())->StdCoreArraySet(index, writeValue);
        return true;
    }
    if (arrHandle.GetPtr()->IsArrayClass()) {
        if (UNLIKELY(!CheckNativeArrayIndex(executionCtx, arrHandle, index))) {
            return true;  // RangeError already thrown
        }
        // EtsObjectArray::Set is a raw store: without this check an
        // unrelated object could be consumed under the declared component
        // type (ArrayStoreError already thrown on mismatch).
        if (UNLIKELY(!CheckReferenceArrayStore(arrHandle, writeValue))) {
            return true;
        }
        auto *objArray = EtsObjectArray::FromCoreType(arrHandle.GetPtr()->GetCoreType());
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        objArray->Set(static_cast<uint32_t>(index), writeValue);
        return true;
    }
    return false;
}

// Whether the object is a std.core.Tuple implementation (the Tuple0..TupleN
// family, or a user class implementing the interface).
static bool IsTupleInstance(EtsExecutionContext *executionCtx, EtsObject *obj)
{
    auto *pt = PlatformTypes(executionCtx);
    return pt != nullptr && pt->coreTuple != nullptr && pt->coreTuple->IsAssignableFrom(obj->GetClass());
}

// Finds an instance field by name along the superclass chain (GetFieldIDByName
// only searches the declaring class; same walk as DebuggerAPIGetEnumValue).
static EtsField *FindFieldInHierarchy(EtsObject *obj, const char *name)
{
    auto *cls = obj->GetClass();
    while (cls != nullptr) {
        auto *field = cls->GetFieldIDByName(name, nullptr);
        if (field != nullptr) {
            return field;
        }
        cls = cls->GetBase();
    }
    return nullptr;
}

// TupleN stores the first TUPLE_INLINE_ELEMENTS in $N fields; the remaining
// elements live in the $tupleValues tail array.
static constexpr int32_t TUPLE_INLINE_ELEMENTS = 16;

// Tuple element write for the identifier-receiver subscript assignment
// (t[0] = v): elements 0..15 live in the public $N fields (erased Object
// storage), the tail of a TupleN lives in the $tupleValues array
// (std.core.Array — resizable, bounds-checked by StdCoreArraySet).
static void WriteTupleElement(EtsExecutionContext *executionCtx, EtsHandle<EtsObject> &tupleHandle, EtsInt index,
                              EtsObject *value)
{
    if (UNLIKELY(index < 0)) {
        ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreRangeError, "Out of bounds");
        return;
    }
    // Pin the value across FindFieldInHierarchy: GetFieldIDByName may trigger
    // class initialization and GC, and a moving collector would invalidate
    // the raw pointer.
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsObject> valueHandle(executionCtx, value);
    auto *field = FindFieldInHierarchy(tupleHandle.GetPtr(), ("$" + std::to_string(index)).c_str());
    if (field != nullptr) {
        tupleHandle.GetPtr()->SetFieldObject(field, valueHandle.GetPtr());
        return;
    }
    // TupleN tail: $tupleValues holds elements with index >= 16
    auto *tailField = FindFieldInHierarchy(tupleHandle.GetPtr(), "$tupleValues");
    if (tailField != nullptr) {
        EtsHandleScope scope(executionCtx);
        auto *tail = tupleHandle.GetPtr()->GetFieldObject(tailField);
        EtsHandle<EtsObject> tailHandle(executionCtx, tail);
        if (tail != nullptr && EtsStdCoreArray::IsExactlyStdCoreArray(tail, executionCtx)) {
            EtsStdCoreArray::FromEtsObject(tail)->StdCoreArraySet(index - TUPLE_INLINE_ELEMENTS, valueHandle.GetPtr());
            return;
        }
        if (tail != nullptr && tail->IsArrayClass()) {
            if (UNLIKELY(!CheckNativeArrayIndex(executionCtx, tailHandle, index - TUPLE_INLINE_ELEMENTS))) {
                return;
            }
            EtsObjectArray::FromCoreType(tail->GetCoreType())
                ->Set(static_cast<uint32_t>(index - TUPLE_INLINE_ELEMENTS), valueHandle.GetPtr());
            return;
        }
    }
    ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreRangeError, "Out of bounds");
}

// Environment for array-element writes: the evaluation context plus the
// paused method's load context (nullptr when unavailable, e.g. the
// value-receiver builtin path), used for declared-type checks.
struct ElementWriteContext {
    EtsExecutionContext *executionCtx;
    ClassLinkerContext *loadContext;
};

static void SetArrayElementWithoutSig(const ElementWriteContext &ewCtx, EtsHandle<EtsObject> &arrHandle,
                                      EtsHandle<EtsObject> &valueHandle, EtsInt index, const std::string &varNameStd)
{
    auto *executionCtx = ewCtx.executionCtx;
    if (EtsStdCoreArray::IsExactlyStdCoreArray(arrHandle.GetPtr(), executionCtx)) {
        // Resizable Array<T>: infer element type from existing elements and widen
        // numeric boxes to match (e.g. Int → Double for number[] arrays), mirroring
        // the ETS compile-time widening semantics.
        auto *buffer = EtsStdCoreArray::FromEtsObject(arrHandle.GetPtr())->GetDataFromStdCoreArray();
        EtsObject *writeValue = ConvertNumericBoxToArrayElement(buffer, index, valueHandle.GetPtr());
        if (writeValue == nullptr) {
            return;
        }
        EtsStdCoreArray::FromEtsObject(arrHandle.GetPtr())->StdCoreArraySet(index, writeValue);
        return;
    }
    if (arrHandle.GetPtr()->IsArrayClass()) {
        // FixedArray<T> / ValueArray<T>: dispatch by the actual storage — the
        // component signature comes from the array class descriptor (see
        // GetComponentSigFromArrayDescriptor for why the EtsClass identity of
        // the component is not usable here).
        char compSig = GetComponentSigFromArrayDescriptor(arrHandle.GetPtr());
        if (compSig != 0) {
            // Primitive component: widen the value to the component type, then
            // write the raw bits into the array data area.
            std::string compSigStr(1, compSig);
            auto vreg =
                ConvertAndUnboxToVRegValue(ewCtx.executionCtx, ewCtx.loadContext, valueHandle.GetPtr(), compSigStr);
            if (!vreg.has_value()) {
                return;  // TypeError already thrown
            }
            WritePrimitiveToFixedArray(executionCtx, arrHandle, *vreg, index);
        } else {
            // Reference component: store the object reference directly,
            // checking component assignability (EtsObjectArray::Set is a
            // raw store; ArrayStoreError already thrown on mismatch).
            if (UNLIKELY(!CheckNativeArrayIndex(executionCtx, arrHandle, index))) {
                return;  // RangeError already thrown
            }
            if (UNLIKELY(!CheckReferenceArrayStore(arrHandle, valueHandle.GetPtr()))) {
                return;  // ArrayStoreError already thrown
            }
            auto *objArray = EtsObjectArray::FromCoreType(arrHandle.GetPtr()->GetCoreType());
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            objArray->Set(static_cast<uint32_t>(index), valueHandle.GetPtr());
        }
        return;
    }
    if (IsTupleInstance(executionCtx, arrHandle.GetPtr())) {
        // Tuple subscript write: elements live in the $N fields (and the
        // $tupleValues tail of a TupleN), invisible to the array paths above.
        WriteTupleElement(executionCtx, arrHandle, index, valueHandle.GetPtr());
        return;
    }
    ThrowDebuggerError(executionCtx, "Unsupported array type for '" + varNameStd + "'");
}

// Finds array variable metadata, throwing a debugger error if not found.
static std::optional<VariableMetaInfo> FindArrayVariableMeta(const VariableSearchContext &ctx,
                                                             const std::string &varNameStd, uint32_t frameOffset)
{
    auto meta = FindVariableMeta(ctx, varNameStd, frameOffset);
    if (!meta) {
        ThrowDebuggerError(ctx.executionCtx, "Variable '" + varNameStd + "' not found");
        return std::nullopt;
    }
    return meta;
}

// Dispatches a primitive vreg to the correct array writer based on array kind.
// Returns false if the array type is unsupported so the caller can report it.
static bool WritePrimitiveToTypedArray(EtsExecutionContext *executionCtx, EtsHandle<EtsObject> &arrHandle,
                                       ark::tooling::VRegValue vreg, const std::string &elementSig, EtsInt index)
{
    if (EtsStdCoreArray::IsExactlyStdCoreArray(arrHandle.GetPtr(), executionCtx)) {
        WritePrimitiveToStdCoreArray(executionCtx, arrHandle, vreg, elementSig, index);
        return true;
    }
    if (arrHandle.GetPtr()->IsArrayClass()) {
        WritePrimitiveToFixedArray(executionCtx, arrHandle, vreg, index);
        return true;
    }
    return false;
}

// Writes an array element with a known element signature.
// Returns false if the array type is unsupported so the caller can report it.
static bool WriteArrayElementWithSig(const ElementWriteContext &ewCtx, EtsHandle<EtsObject> &arrHandle,
                                     EtsObject *value, const std::string &elementSig, EtsInt index)
{
    if (ark::ets::tooling::IsRefSignature(elementSig)) {
        return WriteReferenceArrayElement(ewCtx.executionCtx, arrHandle, value, index);
    }
    auto vreg = ConvertAndUnboxToVRegValue(ewCtx.executionCtx, ewCtx.loadContext, value, elementSig);
    if (!vreg.has_value()) {
        return true;  // TypeError already thrown; not an unsupported-array-type case
    }
    return WritePrimitiveToTypedArray(ewCtx.executionCtx, arrHandle, *vreg, elementSig, index);
}

static EtsHandle<EtsObject> ReadArrayObject(const VariableSearchContext &ctx, int32_t regNumber,
                                            const std::string &varNameStd)
{
    ark::tooling::VRegValue arrVreg;
    auto arrErr = ctx.debugger.GetVariable(ctx.thread, ctx.frameDepth, regNumber, &arrVreg);
    if (arrErr) {
        ThrowDebuggerError(ctx.executionCtx, "Failed to read array variable: " + arrErr.value().GetMessage());
        return EtsHandle<EtsObject>(ctx.executionCtx, nullptr);
    }
    auto *arrayObj = ark::ets::tooling::VRegValueToRefObject(arrVreg);
    if (arrayObj == nullptr) {
        ThrowDebuggerError(ctx.executionCtx, "Array variable '" + varNameStd + "' is null");
        return EtsHandle<EtsObject>(ctx.executionCtx, nullptr);
    }
    return EtsHandle<EtsObject>(ctx.executionCtx, arrayObj);
}

// Locates the named array variable in the paused frame and writes the
// element, dispatching by the declared element signature.
static void ApplyArrayElementWrite(const VariableSearchContext &ctx, const std::string &varNameStd,
                                   EtsHandle<EtsObject> &valueHandle, EtsInt index, uint32_t frameOffset)
{
    auto meta = FindArrayVariableMeta(ctx, varNameStd, frameOffset);
    if (!meta) {
        return;
    }
    auto arrHandle = ReadArrayObject(ctx, meta->regNumber, varNameStd);
    if (arrHandle.GetPtr() == nullptr) {
        return;
    }
    const auto &sig = meta->typeSignature;
    if (!sig.empty() && sig[0] == '[') {
        ElementWriteContext ewCtx {ctx.executionCtx, ctx.loadContext};
        if (!WriteArrayElementWithSig(ewCtx, arrHandle, valueHandle.GetPtr(), sig.substr(1), index)) {
            ThrowDebuggerError(ctx.executionCtx, "Unsupported array type for '" + varNameStd + "'");
        }
        return;
    }
    SetArrayElementWithoutSig(ElementWriteContext {ctx.executionCtx, ctx.loadContext}, arrHandle, valueHandle, index,
                              varNameStd);
}

extern "C" void DebuggerAPISetElement(EtsInt threadId, EtsInt frameDepth, EtsString *name, EtsInt index,
                                      EtsObject *value)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (!CheckDebugPreconditions(executionCtx, name, "Variable name is null")) {
        return;
    }
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> nameHandle(executionCtx, name);
    EtsHandle<EtsObject> valueHandle(executionCtx, value);
    std::string varNameStd(nameHandle->GetMutf8());
    auto &debugger = executionCtx->GetPandaVM()->GetRuntime()->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto frameResult = debugger.GetFrameByThread(thread, static_cast<uint32_t>(frameDepth));
    if (!frameResult) {
        ThrowDebuggerError(executionCtx, "Frame not found");
        return;
    }
    auto *method = (*frameResult)->GetMethod();
    auto *pandaFile = method->GetPandaFile();
    if (UNLIKELY(pandaFile == nullptr)) {
        ThrowDebuggerError(executionCtx, "No panda file for method");
        return;
    }
    panda_file::DebugInfoExtractor debugInfo(pandaFile);
    auto methodId = method->GetFileId();
    VariableSearchContext ctx {debugger,
                               thread,
                               static_cast<uint32_t>(frameDepth),
                               executionCtx,
                               method->GetClass()->GetLoadContext(),
                               debugInfo,
                               methodId,
                               static_cast<uint32_t>((*frameResult)->GetVRegNum())};
    ApplyArrayElementWrite(ctx, varNameStd, valueHandle, index, (*frameResult)->GetBytecodeOffset());
}

static EtsObject *PackRestArgsElement(EtsExecutionContext *executionCtx, EtsHandle<EtsObjectArray> &argsArr,
                                      uint32_t startIdx, uint32_t i, EtsBoolean forceDouble)
{
    EtsHandleScope scope(executionCtx);
    EtsHandle<EtsObject> elem(executionCtx, argsArr->Get(static_cast<uint32_t>(startIdx) + i));
    if ((forceDouble != 0) && elem.GetPtr() != nullptr &&
        GetBoxedNumericValueAsDouble(PlatformTypes(executionCtx), elem.GetPtr()).has_value()) {
        // Retry path: pack numeric elements as Double boxes (number[] rest
        // targets; the first attempt failed in the callee's read-back
        // checkcast with Int boxes).
        auto vreg = ConvertAndUnboxToVRegValue(executionCtx, nullptr, elem.GetPtr(), "D");
        if (vreg.has_value()) {
            elem = EtsHandle<EtsObject>(executionCtx, ark::ets::tooling::BoxVRegValue(*vreg, 'D', executionCtx));
            if (elem.GetPtr() == nullptr) {
                ASSERT(executionCtx->GetMT()->HasPendingException());
                return nullptr;
            }
        }
    }
    return elem.GetPtr();
}

extern "C" EtsObject *DebuggerAPIPackRestArgs(ObjectHeader *argsArr, EtsInt startIdx, EtsBoolean forceDouble)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    ASSERT(executionCtx != nullptr);
    if (UNLIKELY(argsArr == nullptr)) {
        ThrowDebuggerError(executionCtx, "Arguments array is null");
        return nullptr;
    }
    EtsHandleScope scope(executionCtx);
    EtsHandle<EtsObjectArray> argsH(executionCtx, EtsObjectArray::FromCoreType(argsArr));

    auto argsLen = static_cast<int64_t>(argsH->GetLength());
    uint32_t length =
        (startIdx < 0 || static_cast<int64_t>(startIdx) > argsLen) ? 0 : static_cast<uint32_t>(argsLen - startIdx);
    // ETS rest parameters are declared as std.core.Array (the resizable Array<T>
    // mirror class), so the packed array must be an EtsStdCoreArray instance --
    // core Object[] arrays have a different runtime class and fail the
    // CheckReceiverType IsAssignableFrom check.
    auto *result = EtsStdCoreArray::Create(executionCtx, length);
    if (result == nullptr) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }
    EtsHandle<EtsObject> resultH(executionCtx, result->AsObject());
    for (uint32_t i = 0; i < length; ++i) {
        EtsObject *elem = PackRestArgsElement(executionCtx, argsH, static_cast<uint32_t>(startIdx), i, forceDouble);
        if (elem == nullptr) {
            ASSERT(executionCtx->GetMT()->HasPendingException());
            return nullptr;
        }
        EtsStdCoreArray::FromEtsObject(resultH.GetPtr())->StdCoreArraySet(i, elem);
    }
    return resultH.GetPtr();
}

static PandaVector<EtsMethod *> CollectModuleFunctions(const panda_file::File *pandaFile, const std::string &nameStd)
{
    auto *linker = Runtime::GetCurrent()->GetClassLinker();
    PandaVector<EtsMethod *> methods;
    std::string pandaFileName = pandaFile->GetFilename();
    linker->EnumerateClasses([&](Class *klass) -> bool {
        auto *klassFile = klass->GetPandaFile();
        if (klassFile == nullptr || klassFile->GetFilename() != pandaFileName) {
            return true;
        }
        for (auto &method : klass->GetStaticMethods()) {
            auto *etsMethod = EtsMethod::FromRuntimeMethod(&method);
            auto *methodName = etsMethod->GetNameString();
            if (methodName != nullptr && strcmp(methodName->GetMutf8().c_str(), nameStd.c_str()) == 0) {
                methods.emplace_back(etsMethod);
            }
        }
        return true;
    });
    return methods;
}

static ObjectHeader *BuildReflectMethodArray(EtsExecutionContext *executionCtx, const PandaVector<EtsMethod *> &methods)
{
    auto *ptypes = PlatformTypes(executionCtx);
    EtsHandle<EtsObjectArray> arrayH(executionCtx,
                                     EtsObjectArray::Create(ptypes->coreReflectStaticMethod, methods.size()));
    if (UNLIKELY(arrayH.GetPtr() == nullptr)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }
    for (size_t idx = 0; idx < methods.size(); ++idx) {
        auto *reflectMethod = EtsReflectMethod::CreateFromEtsMethod(executionCtx, methods[idx]);
        arrayH->Set(idx, reflectMethod->AsObject());
    }
    return arrayH->AsObject()->GetCoreType();
}

extern "C" ObjectHeader *DebuggerAPIFindModuleFunctions(EtsInt threadId, EtsInt frameDepth, EtsString *name)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (!CheckDebugPreconditions(executionCtx, name, "Function name is null")) {
        return nullptr;
    }
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> nameHandle(executionCtx, name);
    std::string nameStd(nameHandle->GetMutf8());
    auto &debugger = executionCtx->GetPandaVM()->GetRuntime()->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto frameResult = debugger.GetFrameByThread(thread, static_cast<uint32_t>(frameDepth));
    if (!frameResult) {
        ThrowDebuggerError(executionCtx, "Frame not found");
        return nullptr;
    }
    auto *pandaFile = (*frameResult)->GetMethod()->GetPandaFile();
    if (UNLIKELY(pandaFile == nullptr)) {
        ThrowDebuggerError(executionCtx, "No panda file for method");
        return nullptr;
    }
    auto methods = CollectModuleFunctions(pandaFile, nameStd);
    return BuildReflectMethodArray(executionCtx, methods);
}

extern "C" EtsString *DebuggerAPITypeofValue(EtsObject *value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    return EtsGetTypeof(executionCtx, value);
}

// Resolves a source-level class name to its runtime EtsClass.
// First checks platform type names (boolean/byte/short/char/int/long/float/double).
// Then scans ALL classes loaded in the ClassLinker whose EtsClass::GetName()
// simple name matches -- GetName() returns the source-level name, so the
// caller's bare identifier works without knowing the package. This covers
// cross-file / cross-module user types that have been loaded (imported).
// Multiple matches with the same simple name are ambiguous and rejected.
// Finally falls back to boot-context probes for stdlib and default package.
// If the class is not yet loaded, attempts to load it via the frame's
// ClassLinkerContext (for qualified names like "MyNspA.Animal" the class may
// never have been referenced at runtime). GetClass loads+prepares the class
// but does NOT initialize it; the caller is responsible for initialization
// (DebuggerAPIGet checks IsInitialized, newInstance triggers it via the
// constructor).
static EtsClass *ResolvePlatformType(const EtsPlatformTypes *pt, const std::string &className)
{
    if (pt == nullptr) {
        return nullptr;
    }
    if (className == "boolean") {
        return pt->coreBoolean;
    }
    if (className == "byte") {
        return pt->coreByte;
    }
    if (className == "short") {
        return pt->coreShort;
    }
    if (className == "char") {
        return pt->coreChar;
    }
    if (className == "int") {
        return pt->coreInt;
    }
    if (className == "long") {
        return pt->coreLong;
    }
    if (className == "float") {
        return pt->coreFloat;
    }
    if (className == "double") {
        return pt->coreDouble;
    }
    return nullptr;
}

static std::string ResolveTypeAlias(const std::string &className)
{
    static const std::array<std::pair<std::string_view, std::string_view>, 4> TYPE_ALIASES = {{
        {"string", "String"},
        {"number", "Double"},
        {"Number", "Double"},
        {"Any", "Object"},
    }};
    for (auto &[alias, target] : TYPE_ALIASES) {
        if (className == alias) {
            LOG(INFO, DEBUGGER) << "ResolveClassByName: alias '" << alias << "' -> '" << target << "'";
            return std::string(target);
        }
    }
    return className;
}

static EtsClass *FindByDescriptorProbes(ClassLinkerExtension *extension, const std::string &resolvedName)
{
    static const std::array<const char *, 9> STD_PREFIXES = {
        "Lstd/core/", "Lstd/containers/", "Lstd/concurrency/", "Lstd/debug/", "Lstd/interop/",
        "Lstd/math/", "Lstd/consts/",     "Lstd/testing/",     "Lstd/",
    };
    for (auto prefix : STD_PREFIXES) {
        std::string desc = std::string(prefix) + resolvedName + ";";
        auto *klass = extension->FindLoadedClass(reinterpret_cast<const uint8_t *>(desc.c_str()));
        if (klass != nullptr) {
            LOG(INFO, DEBUGGER) << "ResolveClassByName: resolved via descriptor " << desc;
            return EtsClass::FromRuntimeClass(klass);
        }
    }
    return nullptr;
}

// Checks if a class full name matches the resolved name (qualified suffix match or simple name match).
static bool ClassNameMatches(const std::string &fullName, const std::string &resolvedName, bool isQualified,
                             const std::string &qualifiedSuffix)
{
    if (isQualified) {
        return fullName == resolvedName || (fullName.size() > qualifiedSuffix.size() &&
                                            fullName.compare(fullName.size() - qualifiedSuffix.size(),
                                                             qualifiedSuffix.size(), qualifiedSuffix) == 0);
    }
    auto pos = fullName.rfind('.');
    std::string simpleName = (pos == std::string::npos) ? fullName : fullName.substr(pos + 1);
    return simpleName == resolvedName;
}

// On ambiguous matches, prefer the std/core class (e.g. Object, String).
static EtsClass *PreferStdCoreClass(ClassLinker *linker, const std::string &resolvedName, bool isQualified,
                                    const std::string &qualifiedSuffix)
{
    EtsClass *matched = nullptr;
    linker->EnumerateClasses([&](Class *k) -> bool {
        std::string fullName = k->GetName();
        if (fullName.empty()) {
            return true;
        }
        if (ClassNameMatches(fullName, resolvedName, isQualified, qualifiedSuffix) &&
            fullName.find("std/core/") != std::string::npos) {
            matched = EtsClass::FromRuntimeClass(k);
            return false;
        }
        return true;
    });
    return matched;
}

static EtsClass *FindUniqueBySimpleName(ClassLinker *linker, const std::string &resolvedName)
{
    bool isQualified = resolvedName.find('.') != std::string::npos;
    EtsClass *matched = nullptr;
    size_t matchCount = 0;
    std::string qualifiedSuffix = isQualified ? ("." + resolvedName) : std::string();
    linker->EnumerateClasses([&](Class *k) -> bool {
        std::string fullName = k->GetName();
        if (fullName.empty()) {
            return true;
        }
        if (ClassNameMatches(fullName, resolvedName, isQualified, qualifiedSuffix)) {
            matched = EtsClass::FromRuntimeClass(k);
            matchCount++;
        }
        return true;
    });
    if (matchCount == 1) {
        return matched;
    }
    if (matchCount > 1) {
        return PreferStdCoreClass(linker, resolvedName, isQualified, qualifiedSuffix);
    }
    return nullptr;
}

// Tries loading a class via its bare descriptor (L<descName>;).
static EtsClass *TryBareDescriptor(ClassLinkerExtension *extension, const std::string &descName,
                                   ClassLinkerContext *loadCtx, const std::string &className)
{
    std::string desc = "L";
    desc += descName;
    desc += ";";
    SilentErrorHandler silentErr;
    auto *klass = extension->GetClass(reinterpret_cast<const uint8_t *>(desc.c_str()), true, loadCtx, &silentErr);
    if (klass == nullptr) {
        return nullptr;
    }
    LOG(INFO, DEBUGGER) << "ResolveClassByName: loaded class '" << className << "' via descriptor " << desc;
    return EtsClass::FromRuntimeClass(klass);
}

// Scans the context's panda files for class records whose descriptor ends with
// "/<descName>", extracts the module prefix, and tries GetClass with the full
// prefixed descriptor (e.g. "Lmymodule$/MyNspA/Animal;").
static EtsClass *TryPrefixedDescriptors(ClassLinkerExtension *extension, ClassLinkerContext *context,
                                        const std::string &descName, const std::string &className)
{
    // Descriptor wrapper chars: 'L' prefix + ';' suffix.
    size_t descriptorWrapperLen = 2;
    std::string descSuffix = "/" + descName;  // e.g. "/MyNspA/Animal"
    std::set<std::string> triedPrefixes;
    Class *found = nullptr;
    ClassLinkerContext *loadCtx = (context != nullptr) ? context : extension->GetBootContext();
    SilentErrorHandler silentErr;
    context->EnumeratePandaFiles([&](const panda_file::File &pf) -> bool {
        auto classes = pf.GetClasses();
        for (uint32_t offset : classes) {
            panda_file::File::EntityId id(offset);
            if (pf.IsExternal(id)) {
                continue;  // class from another panda file, skip
            }
            panda_file::ClassDataAccessor cda(pf, id);
            auto *descData = cda.GetDescriptor();
            std::string classDesc(reinterpret_cast<const char *>(descData));
            // classDesc looks like "Lmymodule$/SomeClass;".
            if (classDesc.size() < descriptorWrapperLen || classDesc[0] != 'L' || classDesc.back() != ';') {
                continue;
            }
            // Strip "L" prefix and ";" suffix to get the inner descriptor name.
            std::string inner = classDesc.substr(1, classDesc.size() - descriptorWrapperLen);
            // Check if this class descriptor ends with our target suffix.
            auto suffixPos = inner.rfind(descSuffix);
            if (suffixPos == std::string::npos || suffixPos == 0) {
                continue;
            }
            // Extract the module prefix: everything before the suffix.
            std::string prefix = inner.substr(0, suffixPos);
            if (triedPrefixes.count(prefix) > 0) {
                continue;
            }
            triedPrefixes.insert(prefix);
            std::string fullDesc = "L";
            fullDesc += prefix;
            fullDesc += descSuffix;
            fullDesc += ";";
            LOG(INFO, DEBUGGER) << "ResolveClassByName: trying prefixed descriptor " << fullDesc
                                << " (from class record " << classDesc << ")";
            auto *k =
                extension->GetClass(reinterpret_cast<const uint8_t *>(fullDesc.c_str()), true, loadCtx, &silentErr);
            if (k != nullptr) {
                LOG(INFO, DEBUGGER) << "ResolveClassByName: loaded class '" << className << "' via prefixed descriptor "
                                    << fullDesc;
                found = k;
                break;
            }
        }
        return found == nullptr;  // continue to next panda file if not found
    });
    return (found != nullptr) ? EtsClass::FromRuntimeClass(found) : nullptr;
}

static EtsClass *ResolveClassByName([[maybe_unused]] const panda_file::File *pandaFile,
                                    EtsExecutionContext *executionCtx, const std::string &className,
                                    ClassLinkerContext *context)
{
    auto *pt = PlatformTypes(executionCtx);
    auto *platformType = ResolvePlatformType(pt, className);
    if (platformType != nullptr) {
        return platformType;
    }
    auto *linker = Runtime::GetCurrent()->GetClassLinker();
    auto *extension = linker->GetExtension(panda_file::SourceLang::ETS);
    auto resolvedName = ResolveTypeAlias(className);

    auto *probed = FindByDescriptorProbes(extension, resolvedName);
    if (probed != nullptr) {
        return probed;
    }
    auto *unique = FindUniqueBySimpleName(linker, resolvedName);
    if (unique != nullptr) {
        return unique;
    }

    // Final fallback: try to load the class via its descriptor.
    std::string descName = resolvedName;
    std::replace(descName.begin(), descName.end(), '.', '/');
    ClassLinkerContext *loadCtx = (context != nullptr) ? context : extension->GetBootContext();

    auto *bare = TryBareDescriptor(extension, descName, loadCtx, className);
    if (bare != nullptr) {
        return bare;
    }
    if (context != nullptr) {
        auto *prefixed = TryPrefixedDescriptors(extension, context, descName, className);
        if (prefixed != nullptr) {
            return prefixed;
        }
    }
    LOG(ERROR, DEBUGGER) << "ResolveClassByName: class '" << className
                         << "' not found (tried bare descriptor and module-prefixed descriptors)";
    return nullptr;
}

static EtsObject *TryNumericWideningCast(EtsExecutionContext *executionCtx, EtsObject *v, EtsClass *cls)
{
    auto *pt = PlatformTypes(executionCtx);
    if (pt == nullptr) {
        return nullptr;
    }
    auto *vEtsClass = v->GetClass();
    // Target is Double: accept any numeric box, convert to Double
    if (cls == pt->coreDouble) {
        if (auto dval = GetBoxedNumericValueAsDouble(pt, v); dval.has_value()) {
            LOG(ERROR, DEBUGGER) << "castAs: numeric widening " << vEtsClass->GetRuntimeClass()->GetName()
                                 << " -> Double(" << *dval << ")";
            return EtsBoxPrimitive<EtsDouble>::Create(executionCtx, *dval)->AsObject();
        }
    }
    // Target is Float: accept any numeric box, convert to Float
    if (cls == pt->coreFloat) {
        if (auto dval = GetBoxedNumericValueAsDouble(pt, v); dval.has_value()) {
            LOG(ERROR, DEBUGGER) << "castAs: numeric widening " << vEtsClass->GetRuntimeClass()->GetName()
                                 << " -> Float(" << static_cast<float>(*dval) << ")";
            return EtsBoxPrimitive<EtsFloat>::Create(executionCtx, static_cast<float>(*dval))->AsObject();
        }
    }
    // Target is Long: accept integral boxes, convert to Long
    if (cls == pt->coreLong) {
        if (auto ival = GetBoxedIntegralValue(pt, v); ival.has_value()) {
            LOG(ERROR, DEBUGGER) << "castAs: numeric widening " << vEtsClass->GetRuntimeClass()->GetName()
                                 << " -> Long(" << *ival << ")";
            return EtsBoxPrimitive<EtsLong>::Create(executionCtx, *ival)->AsObject();
        }
    }
    // Target is Int: accept smaller integral boxes (byte/short/char), convert to Int
    if (cls == pt->coreInt) {
        if (auto ival = GetBoxedIntegralValue(pt, v); ival.has_value()) {
            LOG(ERROR, DEBUGGER) << "castAs: numeric widening " << vEtsClass->GetRuntimeClass()->GetName() << " -> Int("
                                 << static_cast<int32_t>(*ival) << ")";
            return EtsBoxPrimitive<EtsInt>::Create(executionCtx, static_cast<int32_t>(*ival))->AsObject();
        }
    }
    return nullptr;
}

// Resolves a class name from the debug frame's context: get frame → get
// pandaFile → get loadContext → ResolveClassByName. Throws on frame/class
// not found. Shared by CastAs, ResolveClass, and InstanceofOp.
static EtsClass *ResolveClassFromFrame(EtsExecutionContext *executionCtx, uint32_t frameDepth, const std::string &name)
{
    auto &debugger = executionCtx->GetPandaVM()->GetRuntime()->StartDebugSession()->GetDebugger();
    auto thread = ark::ets::tooling::CoroutineToPtThread(executionCtx->GetMT());
    auto frameResult = debugger.GetFrameByThread(thread, frameDepth);
    if (!frameResult) {
        ThrowDebuggerError(executionCtx, "Frame not found");
        return nullptr;
    }
    auto *method = (*frameResult)->GetMethod();
    auto *pandaFile = method->GetPandaFile();
    if (UNLIKELY(pandaFile == nullptr)) {
        ThrowDebuggerError(executionCtx, "No panda file for method");
        return nullptr;
    }
    auto *frameContext = method->GetClass()->GetLoadContext();
    auto *cls = ResolveClassByName(pandaFile, executionCtx, name, frameContext);
    if (cls == nullptr) {
        ThrowDebuggerError(executionCtx, "Class '" + name + "' not found");
        return nullptr;
    }
    return cls;
}

extern "C" EtsObject *DebuggerAPICastAs(EtsInt threadId, EtsInt frameDepth, EtsObject *v, EtsString *className)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (!CheckDebugPreconditions(executionCtx, className, "castAs: class name is null")) {
        return nullptr;
    }
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> nameHandle(executionCtx, className);
    EtsHandle<EtsObject> vHandle(executionCtx, v);
    auto name = ResolveTypeAlias(std::string(nameHandle->GetMutf8()));
    auto *cls = ResolveClassFromFrame(executionCtx, static_cast<uint32_t>(frameDepth), name);
    if (cls == nullptr) {
        return nullptr;
    }
    if (vHandle.GetPtr() != nullptr && !ark::ets::tooling::IsNullOrNullValue(vHandle.GetPtr()) &&
        !cls->IsAssignableFrom(vHandle.GetPtr()->GetClass())) {
        auto *widened = TryNumericWideningCast(executionCtx, vHandle.GetPtr(), cls);
        if (widened != nullptr) {
            return widened;
        }
        ThrowDebuggerError(executionCtx, "Cannot cast value to '" + name + "'");
        return nullptr;
    }
    return vHandle.GetPtr();
}

extern "C" EtsClass *DebuggerAPIResolveClass(EtsInt threadId, EtsInt frameDepth, EtsString *className)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (!CheckDebugPreconditions(executionCtx, className, "resolveClass: class name is null")) {
        return nullptr;
    }
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> nameHandle(executionCtx, className);
    std::string name(nameHandle->GetMutf8());
    return ResolveClassFromFrame(executionCtx, static_cast<uint32_t>(frameDepth), name);
}

extern "C" EtsBoolean DebuggerAPIInstanceofOp(EtsInt threadId, EtsInt frameDepth, EtsObject *value,
                                              EtsString *className)
{
    (void)threadId;
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (!CheckDebugPreconditions(executionCtx, className, "instanceofOp: class name is null")) {
        return ToEtsBoolean(false);
    }
    if (value == nullptr || ark::ets::tooling::IsNullOrNullValue(value)) {
        return ToEtsBoolean(false);
    }
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsString> nameHandle(executionCtx, className);
    EtsHandle<EtsObject> valueHandle(executionCtx, value);
    std::string name(nameHandle->GetMutf8());
    auto *cls = ResolveClassFromFrame(executionCtx, static_cast<uint32_t>(frameDepth), name);
    if (cls == nullptr) {
        return ToEtsBoolean(false);
    }
    return ToEtsBoolean(cls->IsAssignableFrom(valueHandle.GetPtr()->GetClass()));
}

// Unboxes a numeric primitive and formats it as a string via stringstream.
template <typename T>
static EtsString *FormatBoxedPrimitive(EtsObject *value)
{
    auto val = EtsBoxPrimitive<T>::Unbox(value);
    PandaStringStream pss;
    pss << val;
    return EtsString::CreateFromMUtf8(pss.str().c_str());
}

// Same as above but casts to int before streaming (Byte/Short display as int).
template <typename T>
static EtsString *FormatBoxedPrimitiveAsInt(EtsObject *value)
{
    auto val = EtsBoxPrimitive<T>::Unbox(value);
    PandaStringStream pss;
    pss << static_cast<int>(val);
    return EtsString::CreateFromMUtf8(pss.str().c_str());
}

static EtsString *ConvertBoxedPrimitiveToString(EtsExecutionContext *executionCtx, EtsObject *value)
{
    auto *pt = PlatformTypes(executionCtx);
    if (pt == nullptr) {
        return nullptr;
    }
    auto *valueClass = value->GetClass();
    if (valueClass == pt->coreInt) {
        return FormatBoxedPrimitive<EtsInt>(value);
    }
    if (valueClass == pt->coreLong) {
        return FormatBoxedPrimitive<EtsLong>(value);
    }
    if (valueClass == pt->coreDouble) {
        return FormatBoxedPrimitive<EtsDouble>(value);
    }
    if (valueClass == pt->coreFloat) {
        return FormatBoxedPrimitive<EtsFloat>(value);
    }
    if (valueClass == pt->coreBoolean) {
        auto bval = EtsBoxPrimitive<EtsBoolean>::Unbox(value);
        return EtsString::CreateFromMUtf8(bval != 0 ? "true" : "false");
    }
    if (valueClass == pt->coreByte) {
        return FormatBoxedPrimitiveAsInt<EtsByte>(value);
    }
    if (valueClass == pt->coreShort) {
        return FormatBoxedPrimitiveAsInt<EtsShort>(value);
    }
    if (valueClass == pt->coreChar) {
        auto cval = EtsBoxPrimitive<EtsChar>::Unbox(value);
        auto ch = static_cast<uint16_t>(cval);
        return EtsString::CreateFromUtf16(&ch, 1);
    }
    return nullptr;
}

extern "C" EtsString *DebuggerAPICreateStringInstance(EtsObject *value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();

    if (value == nullptr || ark::ets::tooling::IsNullOrNullValue(value)) {
        return EtsString::CreateFromMUtf8("null");
    }
    if (value->IsStringClass()) {
        return EtsString::FromEtsObject(value);
    }

    // Wrap in a GC-safe handle: ConvertBoxedPrimitiveToString may allocate
    // EtsString objects (triggering GC), after which the raw 'value' pointer
    // would be invalid.
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsObject> valueHandle(executionCtx, value);

    auto *result = ConvertBoxedPrimitiveToString(executionCtx, valueHandle.GetPtr());
    if (result != nullptr) {
        return result;
    }

    auto *valueClass = valueHandle.GetPtr()->GetClass();
    PandaStringStream pss;
    pss << "[" << valueClass->GetRuntimeClass()->GetName() << "]";
    return EtsString::CreateFromMUtf8(pss.str().c_str());
}

// Extracts the underlying value from an enum (BaseEnum subclass) object.
// BaseEnum<T> has a private "value" field of type T (int, double, etc.).
// This reads it via the runtime field API (bypasses ETS public-only reflection).
extern "C" EtsObject *DebuggerAPIGetEnumValue(EtsObject *eObj)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (eObj == nullptr) {
        return nullptr;
    }
    // Pin the object across GetFieldIDByName, which may trigger class
    // initialization and GC: a moving collector invalidates the raw pointer.
    EtsHandleScope handleScope(executionCtx);
    EtsHandle<EtsObject> objHd(executionCtx, eObj);
    auto *cls = objHd->GetClass();
    // Walk the superclass chain to find BaseEnum's "value" field
    while (cls != nullptr) {
        auto *field = cls->GetFieldIDByName("value");
        if (field != nullptr) {
            auto fieldType = field->GetEtsType();
            switch (fieldType) {
                case EtsType::INT:
                    return EtsBoxPrimitive<EtsInt>::Create(executionCtx, objHd->GetFieldPrimitive<EtsInt>(field))
                        ->AsObject();
                case EtsType::LONG:
                    return EtsBoxPrimitive<EtsLong>::Create(executionCtx, objHd->GetFieldPrimitive<EtsLong>(field))
                        ->AsObject();
                case EtsType::DOUBLE:
                    return EtsBoxPrimitive<EtsDouble>::Create(executionCtx, objHd->GetFieldPrimitive<EtsDouble>(field))
                        ->AsObject();
                case EtsType::FLOAT:
                    return EtsBoxPrimitive<EtsFloat>::Create(executionCtx, objHd->GetFieldPrimitive<EtsFloat>(field))
                        ->AsObject();
                case EtsType::SHORT:
                    return EtsBoxPrimitive<EtsShort>::Create(executionCtx, objHd->GetFieldPrimitive<EtsShort>(field))
                        ->AsObject();
                case EtsType::BYTE:
                    return EtsBoxPrimitive<EtsByte>::Create(executionCtx, objHd->GetFieldPrimitive<EtsByte>(field))
                        ->AsObject();
                case EtsType::OBJECT:
                    return objHd->GetFieldObject(field);
                default:
                    return nullptr;
            }
        }
        cls = cls->GetBase();
    }
    return nullptr;
}

// Classifies a receiver for the ETS-level builtin-indexable dispatch (see the
// '$_get' fallback in DebuggerAPI.callInstanceMember): ARRAY = native array
// (FixedArray/ValueArray — subscript is Ldarr semantics, the class has no
// methods at all), TUPLE = std.core.Tuple implementation, NONE = neither.
static constexpr EtsInt BUILTIN_INDEXABLE_NONE = 0;
static constexpr EtsInt BUILTIN_INDEXABLE_ARRAY = 1;
static constexpr EtsInt BUILTIN_INDEXABLE_TUPLE = 2;

extern "C" EtsInt DebuggerAPIIsBuiltinIndexable(EtsObject *obj)
{
    if (obj == nullptr) {
        return BUILTIN_INDEXABLE_NONE;
    }
    if (obj->IsArrayClass()) {
        return BUILTIN_INDEXABLE_ARRAY;
    }
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    auto *pt = PlatformTypes(executionCtx);
    if (pt != nullptr && pt->coreTuple != nullptr && pt->coreTuple->IsAssignableFrom(obj->GetClass())) {
        return BUILTIN_INDEXABLE_TUPLE;
    }
    return BUILTIN_INDEXABLE_NONE;
}

// Reads one native-array element (bounds already checked by the caller) and
// boxes it. Shared by getElementBuiltin and the toResizableArray copy loop.
static EtsObject *ReadNativeArrayElement(EtsExecutionContext *executionCtx, EtsObject *receiver, EtsInt idx)
{
    char compSig = GetComponentSigFromArrayDescriptor(receiver);
    if (compSig == 0) {
        // Reference component: load the element pointer directly.
        return EtsObjectArray::FromCoreType(receiver->GetCoreType())->Get(static_cast<uint32_t>(idx));
    }
    // Primitive component: load elemSize bytes and box by the component sig.
    auto *rawArray = EtsArray::FromEtsObject(receiver);
    // NOLINTNEXTLINE(readability-identifier-naming)
    size_t elemSize = rawArray->GetElementSize();
    auto *data = rawArray->template GetData<uint8_t>();
    int64_t rawValue = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (memcpy_s(&rawValue, elemSize, data + static_cast<size_t>(idx) * elemSize, elemSize) != EOK) {
        ThrowDebuggerError(executionCtx, "failed to read array element");
        return nullptr;
    }
    return ark::ets::tooling::BoxVRegValueBySig(ark::tooling::VRegValue(rawValue), compSig, executionCtx);
}

// Reads a native-array (FixedArray/ValueArray) element with a bounds check
// and boxes it — the evaluate-side equivalent of CheckLoadArrayOp + Ldarr.
// These arrays have no methods, so the proxy-layer method reflection cannot
// reach their subscript semantics; guarded by isBuiltinIndexable == 1 at the
// ETS level.
extern "C" EtsObject *DebuggerAPIGetElementBuiltin(EtsObject *receiver, EtsObject *index)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (UNLIKELY(receiver == nullptr || index == nullptr)) {
        ThrowDebuggerError(executionCtx, "getElementBuiltin: receiver or index is null");
        return nullptr;
    }
    auto idxOpt = GetBoxedIntegralValue(PlatformTypes(executionCtx), index);
    if (UNLIKELY(!idxOpt.has_value())) {
        ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreTypeError, "array index must be an integer");
        return nullptr;
    }
    auto idx = static_cast<EtsInt>(*idxOpt);

    EtsHandleScope scope(executionCtx);
    EtsHandle<EtsObject> arrHandle(executionCtx, receiver);
    if (UNLIKELY(!CheckNativeArrayIndex(executionCtx, arrHandle, idx))) {
        return nullptr;  // RangeError already thrown
    }

    // Dispatch by the actual storage: FixedArray<T> is a boxed object array in
    // this toolchain (e.g. FixedArray<int> is std.core.Int[]), ValueArray<T>
    // is a primitive array — the array class descriptor tells them apart (see
    // GetComponentSigFromArrayDescriptor).
    char compSig = GetComponentSigFromArrayDescriptor(receiver);
    if (compSig == 0) {
        // Reference component: load the element pointer directly.
        return EtsObjectArray::FromCoreType(receiver->GetCoreType())->Get(static_cast<uint32_t>(idx));
    }
    // Primitive component: load elemSize bytes and box by the component sig.
    auto *rawArray = EtsArray::FromEtsObject(receiver);
    // NOLINTNEXTLINE(readability-identifier-naming)
    size_t elemSize = rawArray->GetElementSize();
    auto *data = rawArray->template GetData<uint8_t>();
    int64_t rawValue = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (memcpy_s(&rawValue, elemSize, data + static_cast<size_t>(idx) * elemSize, elemSize) != EOK) {
        ThrowDebuggerError(executionCtx, "failed to read array element");
        return nullptr;
    }
    return ark::ets::tooling::BoxVRegValueBySig(ark::tooling::VRegValue(rawValue), compSig, executionCtx);
}

// Native-array length — the evaluate-side equivalent of the Lenarr
// instruction; guarded by isBuiltinIndexable == 1 at the ETS level.
extern "C" EtsInt DebuggerAPIGetBuiltinLength(EtsObject *receiver)
{
    if (UNLIKELY(receiver == nullptr)) {
        return 0;
    }
    return static_cast<EtsInt>(EtsArray::FromEtsObject(receiver)->GetLength());
}

// Reads one tuple element for the toResizableArray copy loop: elements 0..15
// live in the $N fields, the tail of a TupleN in $tupleValues.
static EtsObject *ReadTupleElementForCopy(EtsExecutionContext *executionCtx, EtsHandle<EtsObject> &tupleHandle,
                                          EtsInt idx)
{
    if (UNLIKELY(idx < 0)) {
        ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreRangeError, "Out of bounds");
        return nullptr;
    }
    auto *field = FindFieldInHierarchy(tupleHandle.GetPtr(), ("$" + std::to_string(idx)).c_str());
    if (field != nullptr) {
        return tupleHandle.GetPtr()->GetFieldObject(field);
    }
    auto *tailField = FindFieldInHierarchy(tupleHandle.GetPtr(), "$tupleValues");
    if (tailField != nullptr) {
        auto *tail = tupleHandle.GetPtr()->GetFieldObject(tailField);
        if (tail != nullptr && EtsStdCoreArray::IsExactlyStdCoreArray(tail, executionCtx)) {
            return EtsStdCoreArray::FromEtsObject(tail)->StdCoreArrayGet(
                idx - TUPLE_INLINE_ELEMENTS);  // throws RangeError OOB
        }
    }
    ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreRangeError, "Out of bounds");
    return nullptr;
}

// Value-receiver subscript write for the builtin indexable kinds (the '$_set'
// miss-path in DebuggerAPI.callInstanceMember): dispatches over the runtime
// kind exactly like the identifier-receiver setElement does — resizable
// (unreachable in practice: the reflection hit path serves std.core.Array),
// native arrays (FixedArray/ValueArray) and tuples.
extern "C" void DebuggerAPISetElementBuiltin(EtsObject *receiver, EtsObject *index, EtsObject *value)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (UNLIKELY(receiver == nullptr || index == nullptr || value == nullptr)) {
        ThrowDebuggerError(executionCtx, "setElementBuiltin: receiver, index or value is null");
        return;
    }
    auto idxOpt = GetBoxedIntegralValue(PlatformTypes(executionCtx), index);
    if (UNLIKELY(!idxOpt.has_value())) {
        ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreTypeError, "array index must be an integer");
        return;
    }
    auto idx = static_cast<EtsInt>(*idxOpt);

    EtsHandleScope scope(executionCtx);
    EtsHandle<EtsObject> arrHandle(executionCtx, receiver);
    EtsHandle<EtsObject> valueHandle(executionCtx, value);
    SetArrayElementWithoutSig(ElementWriteContext {executionCtx, nullptr}, arrHandle, valueHandle, idx,
                              "<value receiver>");
}

// Converts a builtin indexable value (native array / tuple) into a resizable
// std.core.Array copy so that the evaluate spread lowering can iterate it;
// a resizable input is returned as-is (same object). Used by the spread
// bridge in the evaluate transformer — the previous `as Array<Object>` cast
// compiles but fails at runtime for every non-resizable receiver.
extern "C" EtsStdCoreArray *DebuggerAPIToResizableArray(EtsObject *receiver)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (UNLIKELY(receiver == nullptr)) {
        ThrowDebuggerError(executionCtx, "toResizableArray: receiver is null");
        return nullptr;
    }

    EtsHandleScope scope(executionCtx);
    EtsHandle<EtsObject> srcHandle(executionCtx, receiver);

    if (EtsStdCoreArray::IsExactlyStdCoreArray(receiver, executionCtx)) {
        return EtsStdCoreArray::FromEtsObject(receiver);  // already resizable: same object
    }

    if (!receiver->IsArrayClass() && !IsTupleInstance(executionCtx, receiver)) {
        ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreTypeError, "value is not an array or tuple");
        return nullptr;
    }

    // Determine the source length (tuples are not array classes: probe the
    // $N fields and the $tupleValues tail first).
    auto isTuple = IsTupleInstance(executionCtx, srcHandle.GetPtr());
    EtsInt length = 0;
    if (isTuple) {
        while (FindFieldInHierarchy(srcHandle.GetPtr(), ("$" + std::to_string(length)).c_str()) != nullptr) {
            length++;
        }
        auto *tailField = FindFieldInHierarchy(srcHandle.GetPtr(), "$tupleValues");
        if (tailField != nullptr) {
            auto *tail = srcHandle.GetPtr()->GetFieldObject(tailField);
            if (tail != nullptr && EtsStdCoreArray::IsExactlyStdCoreArray(tail, executionCtx)) {
                length += EtsStdCoreArray::FromEtsObject(tail)->GetActualLengthFromStdCoreArray();
            }
        }
    } else {
        length = static_cast<EtsInt>(EtsArray::FromEtsObject(srcHandle.GetPtr())->GetLength());
    }

    auto *result = EtsStdCoreArray::Create(executionCtx, length);
    if (result == nullptr) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }
    EtsHandle<EtsObject> resultHandle(executionCtx, result->AsObject());

    for (EtsInt i = 0; i < length; i++) {
        auto *element = isTuple ? ReadTupleElementForCopy(executionCtx, srcHandle, i)
                                : ReadNativeArrayElement(executionCtx, srcHandle.GetPtr(), i);
        if (element == nullptr && executionCtx->GetMT()->HasPendingException()) {
            return nullptr;
        }
        EtsHandle<EtsObject> elemHandle(executionCtx, element);
        EtsStdCoreArray::FromEtsObject(resultHandle.GetPtr())->StdCoreArraySet(i, elemHandle.GetPtr());
    }
    return EtsStdCoreArray::FromEtsObject(resultHandle.GetPtr());
}

// If `eObj` is an enum (BaseEnum subclass), extracts its underlying boxed
// value via DebuggerAPIGetEnumValue. Returns nullptr if `eObj` is null or
// has no "value" field — GetEnumValue itself walks the hierarchy, resolves
// the field, and returns nullptr for non-enum/unsupported receivers, so no
// pre-check is needed here (which would use the raw pointer across a
// potential GC in GetFieldIDByName).
static EtsObject *UnwrapEnumValueForToConversion(EtsObject *eObj)
{
    return DebuggerAPIGetEnumValue(eObj);
}

// Throws TypeError("cannot convert to <target>"); returns the default value
// of T so the caller can `return ThrowCannotConvert<T>(...)` on a primitive
// return path. The exception is pending on the coroutine, so the runtime
// unwinds before any caller observes the placeholder.
template <typename T>
static T ThrowCannotConvert(EtsExecutionContext *executionCtx, const char *target)
{
    std::string msg = "cannot convert to ";
    msg += target;
    ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreTypeError, msg.c_str());
    return T {};
}

// Template helpers for the To* conversion intrinsics. Three patterns:
// - ToFloatingPoint<T>: any numeric → double → cast to T (Double, Float)
// - ToIntegral<T>: Boolean→0/1, integral widen, float/double truncate (Long, Int, Byte, Short)
// - toChar / toBoolean: standalone (unique conversion rules)

// Floating-point conversions: any numeric box widens to double, then cast to T.
template <typename T>
static T ToFloatingPoint(EtsObject *obj, const char *targetName)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (obj == nullptr) {
        return ThrowCannotConvert<T>(executionCtx, targetName);
    }
    auto *ptypes = PlatformTypes(executionCtx);
    if (auto v = GetBoxedNumericValueAsDouble(ptypes, obj); v.has_value()) {
        return static_cast<T>(*v);
    }
    if (auto *unwrapped = UnwrapEnumValueForToConversion(obj); unwrapped != nullptr) {
        return ToFloatingPoint<T>(unwrapped, targetName);
    }
    return ThrowCannotConvert<T>(executionCtx, targetName);
}

// Integral conversions: Boolean→0/1, integral boxes widen, float/double truncate.
template <typename T>
static T ToIntegral(EtsObject *obj, const char *targetName)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (obj == nullptr) {
        return ThrowCannotConvert<T>(executionCtx, targetName);
    }
    auto *ptypes = PlatformTypes(executionCtx);
    if (obj->GetClass() == ptypes->coreBoolean) {
        return EtsBoxPrimitive<EtsBoolean>::Unbox(obj) ? 1 : 0;
    }
    if (auto iv = GetBoxedIntegralValue(ptypes, obj); iv.has_value()) {
        return static_cast<T>(*iv);
    }
    if (auto dv = GetBoxedNumericValueAsDouble(ptypes, obj); dv.has_value()) {
        return static_cast<T>(*dv);
    }
    if (auto *unwrapped = UnwrapEnumValueForToConversion(obj); unwrapped != nullptr) {
        return ToIntegral<T>(unwrapped, targetName);
    }
    return ThrowCannotConvert<T>(executionCtx, targetName);
}

extern "C" EtsDouble DebuggerAPIToDouble(EtsObject *obj)
{
    return ToFloatingPoint<EtsDouble>(obj, "Double");
}

extern "C" EtsFloat DebuggerAPIToFloat(EtsObject *obj)
{
    return ToFloatingPoint<EtsFloat>(obj, "Float");
}

extern "C" EtsLong DebuggerAPIToLong(EtsObject *obj)
{
    return ToIntegral<EtsLong>(obj, "Long");
}

extern "C" EtsInt DebuggerAPIToInt(EtsObject *obj)
{
    return ToIntegral<EtsInt>(obj, "Int");
}

extern "C" EtsByte DebuggerAPIToByte(EtsObject *obj)
{
    return ToIntegral<EtsByte>(obj, "Byte");
}

extern "C" EtsShort DebuggerAPIToShort(EtsObject *obj)
{
    return ToIntegral<EtsShort>(obj, "Short");
}

// toChar: only integer boxes convert to char (float→char is not a legal ETS
// conversion). Boolean is not accepted (matches the ETS-side behavior).
extern "C" EtsChar DebuggerAPIToChar(EtsObject *obj)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (obj == nullptr) {
        return ThrowCannotConvert<EtsChar>(executionCtx, "Char");
    }
    auto *ptypes = PlatformTypes(executionCtx);
    if (auto iv = GetBoxedIntegralValue(ptypes, obj); iv.has_value()) {
        return static_cast<uint16_t>(*iv);
    }
    if (auto *unwrapped = UnwrapEnumValueForToConversion(obj); unwrapped != nullptr) {
        return DebuggerAPIToChar(unwrapped);
    }
    return ThrowCannotConvert<EtsChar>(executionCtx, "Char");
}

// toBoolean: Boolean is unboxed directly; numerics use zero-value semantics
// (0 → false, non-zero → true).
extern "C" EtsBoolean DebuggerAPIToBoolean(EtsObject *obj)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (obj == nullptr) {
        return ThrowCannotConvert<EtsBoolean>(executionCtx, "Boolean");
    }
    auto *ptypes = PlatformTypes(executionCtx);
    if (obj->GetClass() == ptypes->coreBoolean) {
        return EtsBoxPrimitive<EtsBoolean>::Unbox(obj);
    }
    if (auto iv = GetBoxedIntegralValue(ptypes, obj); iv.has_value()) {
        return static_cast<EtsBoolean>(*iv != 0);
    }
    if (auto dv = GetBoxedNumericValueAsDouble(ptypes, obj); dv.has_value()) {
        return static_cast<EtsBoolean>(*dv != 0.0);
    }
    if (auto *unwrapped = UnwrapEnumValueForToConversion(obj); unwrapped != nullptr) {
        return DebuggerAPIToBoolean(unwrapped);
    }
    return ThrowCannotConvert<EtsBoolean>(executionCtx, "Boolean");
}

// Numeric binary and unary operators for the evaluate transformer.
// The ETS-side wrappers handle String concatenation (for +) and BigInt
// arithmetic (which relies on ETS operator overloading); everything else —
// numeric widening, arithmetic, shifts, comparisons, bitwise ops, and the
// unary neg/plus/bit-not — is implemented here to keep the stdlib abc small.
//
// Opcodes:
//   0=add 1=sub 2=mul 3=div 4=mod 5=pow
//   6=shl 7=shr 8=ushr
//   9=lt 10=gt 11=le 12=ge
//   13=bitAnd 14=bitOr 15=bitXor
namespace {
enum class NumericOp : int32_t {
    ADD = 0,
    SUB = 1,
    MUL = 2,
    DIV = 3,
    MOD = 4,
    POW = 5,
    SHL = 6,
    SHR = 7,
    USHR = 8,
    LT = 9,
    GT = 10,
    LE = 11,
    GE = 12,
    BIT_AND = 13,
    BIT_OR = 14,
    BIT_XOR = 15,
    NEG = 16,
    POS = 17,
    BIT_NOT = 18,
};
}  // namespace

static EtsObject *ThrowInvalidOperand(EtsExecutionContext *executionCtx, const char *op)
{
    std::string msg = "cannot apply operator ";
    msg += op;
    ThrowEtsException(executionCtx, PlatformTypes(executionCtx)->coreTypeError, msg.c_str());
    return nullptr;
}

// Numeric ranks used to pick the common result type of arithmetic ops.
static constexpr int NUMERIC_RANK_INT = 0;
static constexpr int NUMERIC_RANK_LONG = 1;
static constexpr int NUMERIC_RANK_FLOAT = 2;
static constexpr int NUMERIC_RANK_DOUBLE = 3;

// Boxes a result according to the ETS widening rules used by the evaluate
// transformer. The "rank" of the operation (determined by the caller from the
// operands) selects the result type: 0=int, 1=long, 2=float, 3=double.
static EtsObject *BoxNumericResult(EtsExecutionContext *executionCtx, double v, int rank)
{
    switch (rank) {
        case NUMERIC_RANK_DOUBLE:
            return EtsBoxPrimitive<EtsDouble>::Create(executionCtx, v)->AsObject();
        case NUMERIC_RANK_FLOAT:
            return EtsBoxPrimitive<EtsFloat>::Create(executionCtx, static_cast<float>(v))->AsObject();
        case NUMERIC_RANK_LONG:
            return EtsBoxPrimitive<EtsLong>::Create(executionCtx, static_cast<int64_t>(v))->AsObject();
        default:
            return EtsBoxPrimitive<EtsInt>::Create(executionCtx, static_cast<int32_t>(v))->AsObject();
    }
}

// Numeric rank of a boxed value: 3=Double, 2=Float, 1=Long, 0=everything else
// (Int/Short/Byte/Char/Boolean). Used to pick the common result type.
static int NumericRank(EtsPlatformTypes const *ptypes, EtsObject *obj)
{
    auto *cls = obj->GetClass();
    if (cls == ptypes->coreDouble) {
        return NUMERIC_RANK_DOUBLE;
    }
    if (cls == ptypes->coreFloat) {
        return NUMERIC_RANK_FLOAT;
    }
    if (cls == ptypes->coreLong) {
        return NUMERIC_RANK_LONG;
    }
    return NUMERIC_RANK_INT;
}

// Shared context for the three numeric dispatch helpers, bundling all the
// data they need so each helper takes at most 2 parameters.
struct BinaryNumericCtx {
    EtsExecutionContext *executionCtx {nullptr};
    EtsPlatformTypes const *ptypes {nullptr};
    NumericOp op {NumericOp::ADD};
    EtsObject *a {nullptr};
    EtsObject *b {nullptr};
    double av {0.0};
    double bv {0.0};
    std::optional<int64_t> aivOpt {};
    std::optional<int64_t> bivOpt {};
};

// Arithmetic and comparison ops: ADD/SUB/MUL/DIV/MOD/POW box by rank,
// LT/GT/LE/GE return Boolean. Returns nullptr if op is not in this group.
static EtsObject *DispatchArithmeticOrComparison(BinaryNumericCtx const &ctx)
{
    int rank = std::max(NumericRank(ctx.ptypes, ctx.a), NumericRank(ctx.ptypes, ctx.b));
    switch (ctx.op) {
        case NumericOp::ADD:
            return BoxNumericResult(ctx.executionCtx, ctx.av + ctx.bv, rank);
        case NumericOp::SUB:
            return BoxNumericResult(ctx.executionCtx, ctx.av - ctx.bv, rank);
        case NumericOp::MUL:
            return BoxNumericResult(ctx.executionCtx, ctx.av * ctx.bv, rank);
        case NumericOp::DIV:
            return BoxNumericResult(ctx.executionCtx, ctx.av / ctx.bv, rank);
        case NumericOp::MOD:
            return BoxNumericResult(ctx.executionCtx, std::fmod(ctx.av, ctx.bv), rank);
        case NumericOp::POW:
            return BoxNumericResult(ctx.executionCtx, std::pow(ctx.av, ctx.bv), rank);
        case NumericOp::LT:
            return EtsBoxPrimitive<EtsBoolean>::Create(ctx.executionCtx, static_cast<EtsBoolean>(ctx.av < ctx.bv))
                ->AsObject();
        case NumericOp::GT:
            return EtsBoxPrimitive<EtsBoolean>::Create(ctx.executionCtx, static_cast<EtsBoolean>(ctx.av > ctx.bv))
                ->AsObject();
        case NumericOp::LE:
            return EtsBoxPrimitive<EtsBoolean>::Create(ctx.executionCtx, static_cast<EtsBoolean>(ctx.av <= ctx.bv))
                ->AsObject();
        case NumericOp::GE:
            return EtsBoxPrimitive<EtsBoolean>::Create(ctx.executionCtx, static_cast<EtsBoolean>(ctx.av >= ctx.bv))
                ->AsObject();
        default:
            return nullptr;
    }
}

// Shift ops: result type depends only on the left operand (Long/Double→long,
// otherwise int). Right operand is truncated to int. Returns nullptr if op is
// not a shift.
static EtsObject *DispatchShift(BinaryNumericCtx const &ctx, int32_t shift)
{
    int64_t aiv = *ctx.aivOpt;
    bool leftIsLong = (NumericRank(ctx.ptypes, ctx.a) >= NUMERIC_RANK_LONG);
    switch (ctx.op) {
        case NumericOp::SHL: {
            if (leftIsLong) {
                auto shifted = static_cast<int64_t>(aiv << shift);  // NOLINT(hicpp-signed-bitwise)
                return EtsBoxPrimitive<EtsLong>::Create(ctx.executionCtx, shifted)->AsObject();
            }
            auto shifted = static_cast<int32_t>(static_cast<int32_t>(aiv) << shift);  // NOLINT(hicpp-signed-bitwise)
            return EtsBoxPrimitive<EtsInt>::Create(ctx.executionCtx, shifted)->AsObject();
        }
        case NumericOp::SHR: {
            if (leftIsLong) {
                auto shifted = static_cast<int64_t>(aiv >> shift);  // NOLINT(hicpp-signed-bitwise)
                return EtsBoxPrimitive<EtsLong>::Create(ctx.executionCtx, shifted)->AsObject();
            }
            auto shifted = static_cast<int32_t>(static_cast<int32_t>(aiv) >> shift);  // NOLINT(hicpp-signed-bitwise)
            return EtsBoxPrimitive<EtsInt>::Create(ctx.executionCtx, shifted)->AsObject();
        }
        case NumericOp::USHR: {
            if (leftIsLong) {
                auto u = static_cast<uint64_t>(aiv);
                auto shifted = static_cast<int64_t>(u >> static_cast<uint64_t>(shift));
                return EtsBoxPrimitive<EtsLong>::Create(ctx.executionCtx, shifted)->AsObject();
            }
            auto u = static_cast<uint32_t>(static_cast<int32_t>(aiv));
            auto shifted = static_cast<int32_t>(u >> static_cast<uint32_t>(shift));
            return EtsBoxPrimitive<EtsInt>::Create(ctx.executionCtx, shifted)->AsObject();
        }
        default:
            return nullptr;
    }
}

// Bitwise ops: Boolean&Boolean→Boolean, else Long-widening or Int. Returns
// nullptr if op is not a bitwise op.
static EtsObject *DispatchBitwise(BinaryNumericCtx const &ctx)
{
    bool aIsBool = (ctx.a->GetClass() == ctx.ptypes->coreBoolean);
    bool bIsBool = (ctx.b->GetClass() == ctx.ptypes->coreBoolean);
    if (aIsBool && bIsBool) {
        bool avb = EtsBoxPrimitive<EtsBoolean>::Unbox(ctx.a) != 0;
        bool bvb = EtsBoxPrimitive<EtsBoolean>::Unbox(ctx.b) != 0;
        switch (ctx.op) {
            case NumericOp::BIT_AND:
                return EtsBoxPrimitive<EtsBoolean>::Create(ctx.executionCtx, static_cast<EtsBoolean>(avb && bvb))
                    ->AsObject();
            case NumericOp::BIT_OR:
                return EtsBoxPrimitive<EtsBoolean>::Create(ctx.executionCtx, static_cast<EtsBoolean>(avb || bvb))
                    ->AsObject();
            case NumericOp::BIT_XOR:
                return EtsBoxPrimitive<EtsBoolean>::Create(ctx.executionCtx, static_cast<EtsBoolean>(avb != bvb))
                    ->AsObject();
            default:
                return nullptr;
        }
    }
    bool useLong =
        (NumericRank(ctx.ptypes, ctx.a) >= NUMERIC_RANK_LONG || NumericRank(ctx.ptypes, ctx.b) >= NUMERIC_RANK_LONG);
    int64_t aiv2 = ctx.aivOpt.has_value() ? *ctx.aivOpt : static_cast<int64_t>(ctx.av);
    int64_t biv2 = ctx.bivOpt.has_value() ? *ctx.bivOpt : static_cast<int64_t>(ctx.bv);
    switch (ctx.op) {
        case NumericOp::BIT_AND: {
            auto result = aiv2 & biv2;  // NOLINT(hicpp-signed-bitwise)
            return useLong
                       ? EtsBoxPrimitive<EtsLong>::Create(ctx.executionCtx, result)->AsObject()
                       : EtsBoxPrimitive<EtsInt>::Create(ctx.executionCtx, static_cast<int32_t>(result))->AsObject();
        }
        case NumericOp::BIT_OR: {
            auto result = aiv2 | biv2;  // NOLINT(hicpp-signed-bitwise)
            return useLong
                       ? EtsBoxPrimitive<EtsLong>::Create(ctx.executionCtx, result)->AsObject()
                       : EtsBoxPrimitive<EtsInt>::Create(ctx.executionCtx, static_cast<int32_t>(result))->AsObject();
        }
        case NumericOp::BIT_XOR: {
            auto result = aiv2 ^ biv2;  // NOLINT(hicpp-signed-bitwise)
            return useLong
                       ? EtsBoxPrimitive<EtsLong>::Create(ctx.executionCtx, result)->AsObject()
                       : EtsBoxPrimitive<EtsInt>::Create(ctx.executionCtx, static_cast<int32_t>(result))->AsObject();
        }
        default:
            return nullptr;
    }
}

// Dispatches a binary numeric op using double arithmetic and boxes the result
// according to the chosen rank. Comparisons always return Boolean. Shifts use
// the left operand's type for the result and truncate the right to int.
static EtsObject *DispatchBinaryNumeric(EtsExecutionContext *executionCtx, EtsPlatformTypes const *ptypes, NumericOp op,
                                        EtsObject *a, EtsObject *b)
{
    auto avOpt = GetBoxedNumericValueAsDouble(ptypes, a);
    auto bvOpt = GetBoxedNumericValueAsDouble(ptypes, b);
    if (!avOpt.has_value() || !bvOpt.has_value()) {
        return ThrowInvalidOperand(executionCtx, "on non-numeric value");
    }
    BinaryNumericCtx ctx {executionCtx, ptypes, op, a, b, *avOpt, *bvOpt, {}, {}};

    auto *result = DispatchArithmeticOrComparison(ctx);
    if (result != nullptr) {
        return result;
    }

    // Shifts: result type depends only on the left operand (Long/Double→long,
    // otherwise int). Right operand is truncated to int.
    ctx.aivOpt = GetBoxedIntegralValue(ptypes, a);
    if (!ctx.aivOpt.has_value()) {
        return ThrowInvalidOperand(executionCtx, "shift on non-integer value");
    }
    ctx.bivOpt = GetBoxedIntegralValue(ptypes, b);
    int32_t shift = ctx.bivOpt.has_value() ? static_cast<int32_t>(*ctx.bivOpt) : static_cast<int32_t>(ctx.bv);

    result = DispatchShift(ctx, shift);
    if (result != nullptr) {
        return result;
    }

    // Bitwise ops: Boolean&Boolean→Boolean, else Long-widening or Int.
    result = DispatchBitwise(ctx);
    if (result != nullptr) {
        return result;
    }
    return ThrowInvalidOperand(executionCtx, "(unreachable)");
}

extern "C" EtsObject *DebuggerAPINumericBinaryOp(EtsObject *a, EtsObject *b, EtsInt opCode)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (a == nullptr || b == nullptr) {
        return ThrowInvalidOperand(executionCtx, "on null value");
    }
    return DispatchBinaryNumeric(executionCtx, PlatformTypes(executionCtx), static_cast<NumericOp>(opCode), a, b);
}

// Applies a unary op (NEG/POS/BIT_NOT) to a boxed value of type T, returning
// a new boxed value of the same type. Float/Double only support NEG/POS.
template <typename T>
static EtsObject *UnaryOpForType(EtsExecutionContext *executionCtx, EtsObject *a, NumericOp op)
{
    auto v = EtsBoxPrimitive<T>::Unbox(a);
    switch (op) {
        case NumericOp::NEG:
            return EtsBoxPrimitive<T>::Create(executionCtx, static_cast<T>(-v))->AsObject();
        case NumericOp::POS:
            return EtsBoxPrimitive<T>::Create(executionCtx, static_cast<T>(+v))->AsObject();
        case NumericOp::BIT_NOT:
            if constexpr (std::is_integral_v<T>) {
                auto inverted = static_cast<T>(~v);  // NOLINT(hicpp-signed-bitwise)
                return EtsBoxPrimitive<T>::Create(executionCtx, inverted)->AsObject();
            }
            return nullptr;
        default:
            return nullptr;
    }
}

// Unary-op dispatch by the operand's box class: preserves the box type for
// neg/pos/bitNot on the numeric boxes. Returns nullptr when no box matches.
static EtsObject *DispatchUnaryByBoxType(EtsExecutionContext *executionCtx, EtsPlatformTypes const *ptypes,
                                         EtsObject *a, EtsClass *cls, NumericOp op)
{
    if (cls == ptypes->coreDouble) {
        return UnaryOpForType<EtsDouble>(executionCtx, a, op);
    }
    if (cls == ptypes->coreFloat) {
        return UnaryOpForType<EtsFloat>(executionCtx, a, op);
    }
    if (cls == ptypes->coreLong) {
        return UnaryOpForType<EtsLong>(executionCtx, a, op);
    }
    if (cls == ptypes->coreInt) {
        return UnaryOpForType<EtsInt>(executionCtx, a, op);
    }
    if (cls == ptypes->coreShort) {
        return UnaryOpForType<EtsShort>(executionCtx, a, op);
    }
    if (cls == ptypes->coreByte) {
        return UnaryOpForType<EtsByte>(executionCtx, a, op);
    }
    return nullptr;
}

// Unary numeric ops: neg/pos preserve the operand's box type; bitNot returns
// the same box type (Boolean→!Boolean, Double/Float→~Int, Char→~Int).
extern "C" EtsObject *DebuggerAPINumericUnaryOp(EtsObject *a, EtsInt opCode)
{
    auto *executionCtx = EtsExecutionContext::GetCurrent();
    if (a == nullptr) {
        return ThrowInvalidOperand(executionCtx, "on null value");
    }
    auto *ptypes = PlatformTypes(executionCtx);
    auto op = static_cast<NumericOp>(opCode);
    auto *cls = a->GetClass();

    if (op == NumericOp::BIT_NOT && cls == ptypes->coreBoolean) {
        bool v = EtsBoxPrimitive<EtsBoolean>::Unbox(a) != 0;
        return EtsBoxPrimitive<EtsBoolean>::Create(executionCtx, static_cast<EtsBoolean>(!v))->AsObject();
    }

    // For neg/pos on Char, the ETS-side code returns Int (a.toInt() negated).
    // For bitNot on Double/Float/Char, the ETS-side code returns ~Int.
    bool forceInt = (cls == ptypes->coreChar && (op == NumericOp::NEG || op == NumericOp::POS)) ||
                    ((cls == ptypes->coreDouble || cls == ptypes->coreFloat || cls == ptypes->coreChar) &&
                     op == NumericOp::BIT_NOT);
    if (forceInt) {
        auto iv = GetBoxedIntegralValue(ptypes, a);
        int32_t v = iv.has_value() ? static_cast<int32_t>(*iv) : 0;
        switch (op) {
            case NumericOp::NEG:
                return EtsBoxPrimitive<EtsInt>::Create(executionCtx, -v)->AsObject();
            case NumericOp::POS:
                return EtsBoxPrimitive<EtsInt>::Create(executionCtx, +v)->AsObject();
            case NumericOp::BIT_NOT: {
                auto inverted = ~v;  // NOLINT(hicpp-signed-bitwise)
                return EtsBoxPrimitive<EtsInt>::Create(executionCtx, inverted)->AsObject();
            }
            default:
                break;
        }
    }

    if (auto *result = DispatchUnaryByBoxType(executionCtx, ptypes, a, cls, op); result != nullptr) {
        return result;
    }
    return ThrowInvalidOperand(executionCtx, "(unreachable)");
}

// Returns the number of mandatory (non-optional, non-rest) parameters of a
// reflect.Method. Used by the debugger evaluate overload resolution to mirror
// the compiler-side MinArgCount. Accepts any reflect.Method subclass
// (InstanceMethod/StaticMethod/Constructor) since they all share the
// EtsReflectMethod mirror layout.
extern "C" EtsInt DebuggerAPIGetMandatoryArgsNum(ObjectHeader *methodObj)
{
    ASSERT(methodObj != nullptr);
    auto *method = EtsReflectMethod::FromEtsObject(EtsObject::FromCoreType(methodObj))->GetEtsMethod();
    ASSERT(method != nullptr);
    return static_cast<EtsInt>(method->GetNumMandatoryArgs());
}

// Whether the given reflect.Method declares a rest parameter (...args).
extern "C" EtsBoolean DebuggerAPIHasRestParam(ObjectHeader *methodObj)
{
    ASSERT(methodObj != nullptr);
    auto *method = EtsReflectMethod::FromEtsObject(EtsObject::FromCoreType(methodObj))->GetEtsMethod();
    ASSERT(method != nullptr);
    return ToEtsBoolean(method->HasRestParam());
}

}  // namespace ark::ets::intrinsics
