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

#include "annotation_target_visitor.h"
#include "abckit_wrapper/class.h"

bool abckit_wrapper::AnnotationTargetVisitor::operator()(Namespace *) const
{
    return true;
}

bool abckit_wrapper::AnnotationTargetVisitor::operator()(Method *method) const
{
    return method->AnnotationsAccept(this->visitor_);
}

bool abckit_wrapper::AnnotationTargetVisitor::operator()(Field *field) const
{
    return field->AnnotationsAccept(this->visitor_);
}

bool abckit_wrapper::AnnotationTargetVisitor::operator()(Class *clazz) const
{
    return clazz->AnnotationsAccept(this->visitor_);
}

bool abckit_wrapper::AnnotationTargetVisitor::operator()(AnnotationInterface *ai) const
{
    return true;
}
