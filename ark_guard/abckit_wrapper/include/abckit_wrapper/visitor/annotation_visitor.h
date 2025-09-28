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

#ifndef ABCKIT_WRAPPER_ANNOTATION_INTERFACE_VISITOR_H
#define ABCKIT_WRAPPER_ANNOTATION_INTERFACE_VISITOR_H

#include "visitor_wrapper.h"

namespace abckit_wrapper {

class AnnotationInterface;
class Annotation;

/**
 * @brief AnnotationInterfaceVisitor
 */
class AnnotationInterfaceVisitor {
public:
    /**
     * @brief AnnotationInterfaceVisitor default Destructor
     */
    virtual ~AnnotationInterfaceVisitor() = default;

    /**
     * @brief Visit annotationInterface
     * @param ai visited annotationInterface
     * @return `false` if was early exited. Otherwise - `true`.
     */
    virtual bool Visit(AnnotationInterface *ai) = 0;
};

/**
 * @brief AnnotationVisitor
 */
class AnnotationVisitor : public WrappedVisitor<AnnotationVisitor> {
public:
    /**
     * @brief Visit annotation
     * @param annotation visited annotation
     * @return `false` if was early exited. Otherwise - `true`.
     */
    virtual bool Visit(Annotation *annotation) = 0;
};

}  // namespace abckit_wrapper

#endif  // ABCKIT_WRAPPER_ANNOTATION_INTERFACE_VISITOR_H
