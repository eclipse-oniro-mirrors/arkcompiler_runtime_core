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

#ifndef ABCKIT_WRAPPER_ANNOTATION_LINKER_H
#define ABCKIT_WRAPPER_ANNOTATION_LINKER_H

#include "abckit_wrapper/file_view.h"

namespace abckit_wrapper {

/**
 * @brief AnnotationLinker
 * link all annotation's AnnotationInterface
 */
class AnnotationLinker final : public AnnotationVisitor {
public:
    /**
     * @brief AnnotationLinker Constructor
     * @param fileView fileView
     */
    explicit AnnotationLinker(FileView &fileView) : fileView_(fileView) {}

    /**
     * @brief Link all annotation's AnnotationInterface
     * @return AbckitWrapperErrorCode
     */
    AbckitWrapperErrorCode Link();

private:
    bool Visit(Annotation *annotation) override;

    FileView &fileView_;
    AbckitWrapperErrorCode result = ERR_SUCCESS;
};
}  // namespace abckit_wrapper

#endif  // ABCKIT_WRAPPER_ANNOTATION_LINKER_H
