/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "annotation_linker.h"
#include "full_name_util.h"
#include "logger.h"

AbckitWrapperErrorCode abckit_wrapper::AnnotationLinker::Link()
{
    this->fileView_.ModulesAccept(this->Wrap<ModuleAnnotationVisitor>().Wrap<LocalModuleFilter>());
    return this->result;
}

bool abckit_wrapper::AnnotationLinker::Visit(Annotation *annotation)
{
    if (annotation->IsExternal()) {
        return true;
    }

    const auto fullName = FullNameUtil::GetFullName(annotation->impl_.GetInterface());
    const auto ai = this->fileView_.Get<AnnotationInterface>(fullName);
    if (!ai.has_value()) {
        this->result = ERR_INNER_ERROR;
        LOG_E << "Failed to get annotation interface:" << fullName;
        return false;
    }
    LOG_I << "Link annotation " << annotation->GetName() << " to " << fullName;
    annotation->ai_ = ai;

    return true;
}
