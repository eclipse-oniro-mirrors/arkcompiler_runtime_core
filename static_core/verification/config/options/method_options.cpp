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

#include "method_options.h"
#include "verification/verifier_messages_data.h"

namespace panda::verifier {

MethodOption::MsgClass MethodOptions::MsgClassFor(VerifierMessage msg_num) const
{
    if (CanHandleMsg(msg_num)) {
        return msg_classes_.at(msg_num);
    }
    for (const auto &up : uplevel_) {
        if (up.get().CanHandleMsg(msg_num)) {
            return up.get().MsgClassFor(msg_num);
        }
    }
    return GetDefaultClassForMessage(msg_num);
}

}  // namespace panda::verifier
