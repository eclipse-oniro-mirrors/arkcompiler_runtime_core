/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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
#ifndef PANDA_PLUGINS_ETS_RUNTIME_ETS_CLASS_LINKER_EXTENSION_H
#define PANDA_PLUGINS_ETS_RUNTIME_ETS_CLASS_LINKER_EXTENSION_H

#include <cstddef>

#include <libpandafile/include/source_lang_enum.h>

#include "libpandabase/macros.h"
#include "runtime/include/class_linker_extension.h"
#include "runtime/include/class_linker.h"
#include "runtime/include/class_root.h"
#include "runtime/include/mem/panda_string.h"
#include "plugins/ets/runtime/types/ets_class.h"
#include "plugins/ets/runtime/ets_coroutine.h"

namespace panda {

class Class;
class Method;
class ObjectHeader;

}  // namespace panda

namespace panda::ets {

class EtsClassLinkerExtension : public ClassLinkerExtension {
public:
    EtsClassLinkerExtension() : ClassLinkerExtension(panda_file::SourceLang::ETS) {}

    ~EtsClassLinkerExtension() override;

    bool InitializeArrayClass(Class *arrayClass, Class *componentClass) override;

    void InitializePrimitiveClass(Class *primitiveClass) override;

    size_t GetClassVTableSize(ClassRoot root) override;

    size_t GetClassIMTSize(ClassRoot root) override;

    size_t GetClassSize(ClassRoot root) override;

    size_t GetArrayClassVTableSize() override;

    size_t GetArrayClassIMTSize() override;

    size_t GetArrayClassSize() override;

    Class *CreateClass(const uint8_t *descriptor, size_t vtableSize, size_t imtSize, size_t size) override;

    void FreeClass(Class *klass) override;

    bool InitializeClass([[maybe_unused]] Class *klass) override;

    const void *GetNativeEntryPointFor([[maybe_unused]] Method *method) const override;

    bool CanThrowException([[maybe_unused]] const Method *method) const override
    {
        return true;
    }

    ClassLinkerErrorHandler *GetErrorHandler() override
    {
        return &errorHandler_;
    };

    Class *FromClassObject(panda::ObjectHeader *obj) override;
    size_t GetClassObjectSizeFromClassSize(uint32_t size) override;

    Class *GetObjectClass()
    {
        return objectClass_;
    }

    Class *GetPromiseClass()
    {
        return promiseClass_;
    }

    Class *GetArrayBufferClass()
    {
        return arraybufClass_;
    }

    Class *GetSharedMemoryClass()
    {
        return sharedMemoryClass_;
    }

    Class *GetTypeAPIFieldClass()
    {
        return typeapiFieldClass_;
    }

    Class *GetTypeAPIMethodClass()
    {
        return typeapiMethodClass_;
    }

    Class *GetTypeAPIParameterClass()
    {
        return typeapiParameterClass_;
    }

    Class *GetVoidClass()
    {
        return voidClass_;
    }

    Class *GetBoxBooleanClass()
    {
        return boxBooleanClass_;
    }

    Class *GetBoxByteClass()
    {
        return boxByteClass_;
    }

    Class *GetBoxCharClass()
    {
        return boxCharClass_;
    }

    Class *GetBoxShortClass()
    {
        return boxShortClass_;
    }

    Class *GetBoxIntClass()
    {
        return boxIntClass_;
    }

    Class *GetBoxLongClass()
    {
        return boxLongClass_;
    }

    Class *GetBoxFloatClass()
    {
        return boxFloatClass_;
    }

    Class *GetBoxDoubleClass()
    {
        return boxDoubleClass_;
    }

    static EtsClassLinkerExtension *FromCoreType(ClassLinkerExtension *ext)
    {
        ASSERT(ext->GetLanguage() == panda_file::SourceLang::ETS);
        return static_cast<EtsClassLinkerExtension *>(ext);
    }

    LanguageContext GetLanguageContext() const
    {
        return langCtx_;
    }

    NO_COPY_SEMANTIC(EtsClassLinkerExtension);
    NO_MOVE_SEMANTIC(EtsClassLinkerExtension);

private:
    bool InitializeImpl(bool compressedStringEnabled) override;

    Class *InitializeClass(ObjectHeader *objectHeader, const uint8_t *descriptor, size_t vtableSize, size_t imtSize,
                           size_t size);

    Class *CreateClassRoot(const uint8_t *descriptor, ClassRoot root);

    bool CacheClass(Class **classForCache, const char *descriptor);

    class ErrorHandler : public ClassLinkerErrorHandler {
    public:
        void OnError(ClassLinker::Error error, const PandaString &message) override;
    };

    ErrorHandler errorHandler_;
    LanguageContext langCtx_ {nullptr};
    mem::HeapManager *heapManager_ {nullptr};

    // void class
    Class *voidClass_ = nullptr;

    // Box classes
    Class *boxBooleanClass_ = nullptr;
    Class *boxByteClass_ = nullptr;
    Class *boxCharClass_ = nullptr;
    Class *boxShortClass_ = nullptr;
    Class *boxIntClass_ = nullptr;
    Class *boxLongClass_ = nullptr;
    Class *boxFloatClass_ = nullptr;
    Class *boxDoubleClass_ = nullptr;

    // Cached classes
    Class *objectClass_ = nullptr;
    Class *promiseClass_ = nullptr;
    Class *arraybufClass_ = nullptr;

    // Cached type API classes
    Class *typeapiFieldClass_ = nullptr;
    Class *typeapiMethodClass_ = nullptr;
    Class *typeapiParameterClass_ = nullptr;

    // Escompat classes
    Class *sharedMemoryClass_ = nullptr;
};

}  // namespace panda::ets

#endif  // !PANDA_PLUGINS_ETS_RUNTIME_ETS_CLASS_LINKER_EXTENSION_H
