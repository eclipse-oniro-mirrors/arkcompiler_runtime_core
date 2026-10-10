/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#ifndef PANDA_RUNTIME_INCLUDE_TOOLING_PT_LANG_EXTENSION_H
#define PANDA_RUNTIME_INCLUDE_TOOLING_PT_LANG_EXTENSION_H

#include "libarkbase/macros.h"
#include "libarkfile/file_items.h"
#include "runtime/include/typed_value.h"

#include <optional>
#include <functional>
#include <string>

namespace ark {
class Field;
class ManagedThread;
class ClassLinker;
}  // namespace ark

namespace ark::tooling {
class PtLangExt {
public:
    using PropertyHandler =
        std::function<void(const std::string &name, TypedValue value, bool isFinal, bool isAccessor)>;

public:
    PtLangExt() = default;
    virtual ~PtLangExt() = default;

    NO_COPY_SEMANTIC(PtLangExt);
    NO_MOVE_SEMANTIC(PtLangExt);

    virtual std::string GetClassName(const ObjectHeader *object) = 0;
    virtual std::optional<std::string> GetAsString(const ObjectHeader *object) = 0;
    virtual std::optional<size_t> GetLengthIfArray(const ObjectHeader *object) = 0;
    virtual void EnumerateProperties(const ObjectHeader *object, const PropertyHandler &handler) = 0;
    virtual void EnumerateGlobals(const PropertyHandler &handler) = 0;
    virtual bool IsWeakReferentField([[maybe_unused]] ObjectHeader *object, [[maybe_unused]] const Field &field)
    {
        return false;
    }
    virtual std::string_view GetThisParameterName() const
    {
        return "this";
    }
    virtual bool CollectHybridStackFrames(
        [[maybe_unused]] const std::function<void(const void *frame, bool isStaticFrame)> &callback)
    {
        return false;
    }
    virtual size_t GetDynamicFrameInfo([[maybe_unused]] const void *frame, [[maybe_unused]] void *buffer,
                                       [[maybe_unused]] size_t bufferSize, [[maybe_unused]] uintptr_t *fileId,
                                       [[maybe_unused]] uint32_t *bcOffset)
    {
        return 0;
    }
    virtual bool IsSupportHybridStack()
    {
        return false;
    }
    virtual std::optional<std::string> GetAsBigIntString([[maybe_unused]] const ObjectHeader *object)
    {
        return {};
    }
    virtual std::optional<TypedValue> GetBoxedPrimitiveValue([[maybe_unused]] const ObjectHeader *object)
    {
        return {};
    }
    // For enum objects: returns the underlying boxed-primitive value (the
    // `value` field of BaseEnum).  Returns std::nullopt if the object is not
    // an ETS enum or the value field cannot be read.
    virtual std::optional<TypedValue> GetEnumValue([[maybe_unused]] const ObjectHeader *object)
    {
        return {};
    }
    // Called by the debugger before invoking the evaluation entry method.
    // Language plugins can initialize any classes that the generated
    // evaluation bytecode depends on (e.g. a debugger helper API class
    // whose static methods are called from the synthetic runtime_evaluate
    // function). The default implementation is a no-op.
    virtual void PrepareEvaluationClasses([[maybe_unused]] ManagedThread *managedThread,
                                          [[maybe_unused]] ClassLinker *linker)
    {
    }
    // Returns true if the object is the language's null/undefined singleton
    // (e.g. ETS NullValue). The debugger serializes such objects as CDP null
    // rather than a generic object. The default implementation returns false.
    virtual bool IsNullOrUndefined([[maybe_unused]] const ObjectHeader *object)
    {
        return false;
    }
};
}  // namespace ark::tooling

#endif  // PANDA_RUNTIME_INCLUDE_TOOLING_PT_LANG_EXTENSION_H
