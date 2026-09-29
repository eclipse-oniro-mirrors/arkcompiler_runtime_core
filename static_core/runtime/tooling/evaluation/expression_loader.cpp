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

#include "runtime/tooling/evaluation/expression_loader.h"

#include "libarkbase/utils/span.h"
#include "libarkbase/utils/utf.h"
#include "libarkfile/class_data_accessor.h"
#include "runtime/include/runtime.h"

namespace ark::tooling {

namespace {

bool ValidateEvaluationMethod(Method *method)
{
    if (method == nullptr) {
        LOG(WARNING, DEBUGGER) << "Evaluate failed to find method: method is null "
                               << "(likely a cascading failure from class loading)";
        return false;
    }
    // Allow 0 args (legacy) or 2 args (thread: int, frame: int)
    static constexpr size_t EVALUATION_METHOD_ARGS_COUNT = 2;
    auto numArgs = method->GetNumArgs();
    if (numArgs != 0 && numArgs != EVALUATION_METHOD_ARGS_COUNT) {
        LOG(WARNING, DEBUGGER) << "Evaluation method must have 0 or 2 arguments, but got " << numArgs;
        return false;
    }
    // Allow primitive and reference return types
    auto type = method->GetReturnType();
    if (!type.IsPrimitive() && !type.IsReference()) {
        LOG(WARNING, DEBUGGER) << "Evaluation method can return only primitive or reference values";
        return false;
    }

    return true;
}

/**
 * @brief Finds evaluation class by looking for a static method named "runtime_evaluate".
 * @param pf received panda file.
 * @returns pair of evaluation method name and class data accessor on success, nullopt otherwise.
 */
static bool HasStaticMethodNamed(const panda_file::File *pf, panda_file::File::EntityId classId,
                                 std::string_view methodName)
{
    panda_file::ClassDataAccessor cda(*pf, classId);
    bool found = false;
    cda.EnumerateMethods([&found, &methodName](panda_file::MethodDataAccessor &mda) {
        if (mda.IsStatic() && !mda.IsExternal()) {
            auto nameData = mda.GetName();
            if (utf::IsEqual(utf::CStringAsMutf8(methodName.data()), nameData.data)) {
                found = true;
            }
        }
    });
    return found;
}

std::optional<std::pair<std::string, panda_file::ClassDataAccessor>> FindEvaluationMethodClass(
    const panda_file::File *pf)
{
    static constexpr std::string_view ENTRY_METHOD_NAME = "runtime_evaluate";
    for (uint32_t id : pf->GetClasses()) {
        panda_file::File::EntityId entityId(id);
        if (pf->IsExternal(entityId)) {
            continue;
        }
        if (HasStaticMethodNamed(pf, entityId, ENTRY_METHOD_NAME)) {
            panda_file::ClassDataAccessor cda(*pf, entityId);
            return std::make_pair(std::string(ENTRY_METHOD_NAME), std::move(cda));
        }
    }
    return {};
}

Method *LoadFileAndGetEntryMethod(ClassLinkerContext *ctx, std::unique_ptr<const panda_file::File> &&pf)
{
    auto optEvalClassInfo = FindEvaluationMethodClass(pf.get());
    if (!optEvalClassInfo) {
        LOG(WARNING, DEBUGGER) << "Evaluate failed to find entry class in the eval bytecode";
        return nullptr;
    }
    auto [methodName, cda] = *optEvalClassInfo;
    auto evalClassId = cda.GetClassId();
    const auto *descriptor = pf->GetStringData(evalClassId).data;
    auto descriptorStr = utf::Mutf8AsCString(descriptor);
    auto *linker = Runtime::GetCurrent()->GetClassLinker();
    [[maybe_unused]] auto sourceLang = cda.GetSourceLang().value_or(panda_file::SourceLang::PANDA_ASSEMBLY);
    ASSERT(linker->GetExtension(sourceLang) != nullptr);
    auto *loadCtx = ctx;
    auto *existingClass = linker->GetClass(*pf, evalClassId, loadCtx, nullptr);
    if (existingClass != nullptr) {
        LOG(INFO, DEBUGGER) << "Evaluation class " << descriptorStr << " already loaded, reusing existing method";
        return existingClass->GetDirectMethod(utf::CStringAsMutf8(methodName.c_str()));
    }
    const panda_file::File *rawPf = pf.get();
    const std::string pfFilename = pf->GetFilename();
    linker->AddPandaFile(std::move(pf), loadCtx);
    LOG(INFO, DEBUGGER) << "Evaluate: added panda file " << pfFilename << " to context";
    LOG(INFO, DEBUGGER) << "Evaluate: loading class " << descriptorStr << " evalClassId=" << evalClassId.GetOffset();
    auto *klass = linker->LoadClass(*rawPf, evalClassId, loadCtx, nullptr);
    LOG(INFO, DEBUGGER) << "Evaluate: LoadClass returned " << (klass != nullptr ? "non-null" : "null");
    if (klass == nullptr) {
        LOG(WARNING, DEBUGGER) << "Evaluate failed to load class " << descriptorStr;
        return nullptr;
    }
    return klass->GetDirectMethod(utf::CStringAsMutf8(methodName.c_str()));
}

}  // namespace

Expected<Method *, Error> LoadExpressionBytecode(ClassLinkerContext *ctx, const std::string &bytecode)
{
    auto pf = ark::panda_file::OpenPandaFileFromMemory(bytecode.data(), bytecode.size());
    if (pf == nullptr) {
        return Unexpected(Error(Error::Type::INVALID_EXPRESSION, "failed to load panda file"));
    }

    auto *method = LoadFileAndGetEntryMethod(ctx, std::move(pf));
    if (!ValidateEvaluationMethod(method)) {
        return Unexpected(Error(Error::Type::INVALID_ENTRY_POINT, "invalid expression bytecode"));
    }
    return method;
}

}  // namespace ark::tooling
