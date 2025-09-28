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
#include "abckit_wrapper/visitor/member_visitor.h"

#include "abckit_wrapper/member.h"

bool abckit_wrapper::MemberFilter::Visit(Member *member)
{
    if (this->Accepted(member)) {
        return member->MemberAccept(visitor_);
    }

    return true;
}

bool abckit_wrapper::MemberAccessFlagFilter::Accepted(Member *member)
{
    if ((member->GetAccessFlags() & this->rejectFlags_) != 0) {
        return false;
    }

    return (member->GetAccessFlags() & this->accessFlags_) != 0;
}