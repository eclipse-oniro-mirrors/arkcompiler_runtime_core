/**
 * Copyright (c) 2023-2025 Huawei Device Co., Ltd.
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

#include "plugins/ets/runtime/ets_annotation.h"
#include "plugins/ets/runtime/ets_class_linker.h"
#include "plugins/ets/runtime/ets_class_linker_extension.h"
#include "plugins/ets/runtime/ets_coroutine.h"
#include "plugins/ets/runtime/ets_exceptions.h"
#include "plugins/ets/runtime/ets_panda_file_items.h"
#include "plugins/ets/runtime/types/ets_class.h"
#include "types/ets_field.h"

namespace ark::ets {

EtsClassLinker::EtsClassLinker(ClassLinker *classLinker) : classLinker_(classLinker) {}

/*static*/
Expected<PandaUniquePtr<EtsClassLinker>, PandaString> EtsClassLinker::Create(ClassLinker *classLinker)
{
    PandaUniquePtr<EtsClassLinker> etsClassLinker = MakePandaUnique<EtsClassLinker>(classLinker);
    return Expected<PandaUniquePtr<EtsClassLinker>, PandaString>(std::move(etsClassLinker));
}

bool EtsClassLinker::Initialize()
{
    ClassLinkerExtension *ext = classLinker_->GetExtension(panda_file::SourceLang::ETS);
    ext_ = EtsClassLinkerExtension::FromCoreType(ext);
    return true;
}

bool EtsClassLinker::InitializeClass(EtsCoroutine *coroutine, EtsClass *klass)
{
    return classLinker_->InitializeClass(coroutine, klass->GetRuntimeClass());
}

EtsClass *EtsClassLinker::GetClassRoot(EtsClassRoot root) const
{
    return EtsClass::FromRuntimeClass(ext_->GetClassRoot(static_cast<ark::ClassRoot>(root)));
}

EtsClass *EtsClassLinker::GetClass(const char *name, bool needCopyDescriptor, ClassLinkerContext *classLinkerContext,
                                   ClassLinkerErrorHandler *errorHandler)
{
    const uint8_t *classDescriptor = utf::CStringAsMutf8(name);
    Class *cls = ext_->GetClass(classDescriptor, needCopyDescriptor, classLinkerContext, errorHandler);
    return LIKELY(cls != nullptr) ? EtsClass::FromRuntimeClass(cls) : nullptr;
}

EtsClass *EtsClassLinker::GetClass(const panda_file::File &pf, panda_file::File::EntityId id,
                                   ClassLinkerContext *classLinkerContext, ClassLinkerErrorHandler *errorHandler)
{
    Class *cls = ext_->GetClass(pf, id, classLinkerContext, errorHandler);
    return LIKELY(cls != nullptr) ? EtsClass::FromRuntimeClass(cls) : nullptr;
}

Method *EtsClassLinker::GetMethod(const panda_file::File &pf, panda_file::File::EntityId id)
{
    return classLinker_->GetMethod(pf, id);
}

Method *EtsClassLinker::GetAsyncImplMethod(Method *method, EtsCoroutine *coroutine)
{
    panda_file::File::EntityId asyncAnnId = EtsAnnotation::FindAsyncAnnotation(method);
    ASSERT(asyncAnnId.IsValid());
    const panda_file::File &pf = *method->GetPandaFile();
    panda_file::AnnotationDataAccessor ada(pf, asyncAnnId);
    auto implMethodId = ada.GetElement(0).GetScalarValue().Get<panda_file::File::EntityId>();
    Method *result = GetMethod(pf, implMethodId);
    if (result == nullptr) {
        panda_file::MethodDataAccessor mda(pf, implMethodId);
        PandaStringStream out;
        out << "Cannot resolve async method " << mda.GetFullName();
        ThrowEtsException(coroutine, panda_file_items::class_descriptors::NO_SUCH_METHOD_ERROR, out.str());
    }
    return result;
}

EtsClass *EtsClassLinker::GetObjectClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetObjectClass());
}

EtsClass *EtsClassLinker::GetPromiseClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetPromiseClass());
}

EtsClass *EtsClassLinker::GetPromiseRefClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetPromiseRefClass());
}

EtsClass *EtsClassLinker::GetWaitersListClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetWaitersListClass());
}

EtsClass *EtsClassLinker::GetMutexClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetMutexClass());
}

EtsClass *EtsClassLinker::GetEventClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetEventClass());
}

EtsClass *EtsClassLinker::GetCondVarClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetCondVarClass());
}

EtsClass *EtsClassLinker::GetArrayClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetArrayClass());
}

EtsClass *EtsClassLinker::GetArrayBufferClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetArrayBufferClass());
}

EtsClass *EtsClassLinker::GetStringBuilderClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetStringBuilderClass());
}

EtsClass *EtsClassLinker::GetSharedMemoryClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetSharedMemoryClass());
}

EtsClass *EtsClassLinker::GetTypeAPIFieldClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetTypeAPIFieldClass());
}

EtsClass *EtsClassLinker::GetTypeAPIMethodClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetTypeAPIMethodClass());
}

EtsClass *EtsClassLinker::GetTypeAPIParameterClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetTypeAPIParameterClass());
}

EtsClass *EtsClassLinker::GetFunctionClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetFunctionClass());
}

EtsClass *EtsClassLinker::GetFinalizableWeakRefClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetFinalizableWeakRefClass());
}

EtsClass *EtsClassLinker::GetRuntimeLinkerClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetRuntimeLinkerClass());
}

EtsClass *EtsClassLinker::GetBootRuntimeLinkerClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetBootRuntimeLinkerClass());
}

EtsClass *EtsClassLinker::GetAbcRuntimeLinkerClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetAbcRuntimeLinkerClass());
}

EtsClass *EtsClassLinker::GetAbcFileClass()
{
    return EtsClass::FromRuntimeClass(ext_->GetAbcFileClass());
}

Method *EtsClassLinker::GetSubscribeOnAnotherPromiseMethod()
{
    return ext_->GetSubscribeOnAnotherPromiseMethod();
}

Method *EtsClassLinker::GetFinalizationRegistryExecCleanupMethod()
{
    return ext_->GetFinalizationRegistryExecCleanupMethod();
}

}  // namespace ark::ets
