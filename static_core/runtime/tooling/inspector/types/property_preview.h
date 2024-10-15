/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef PANDA_TOOLING_INSPECTOR_TYPES_PROPERTY_PREVIEW_H
#define PANDA_TOOLING_INSPECTOR_TYPES_PROPERTY_PREVIEW_H

#include "tooling/inspector/types/remote_object_type.h"

#include <optional>
#include <string>

namespace ark::tooling::inspector {

class PropertyPreview final {
public:
    PropertyPreview(std::string name, RemoteObjectType type) : name_(std::move(name)), type_(type) {}

    PropertyPreview(std::string name, RemoteObjectType type, const std::string &value)
        : PropertyPreview(std::move(name), type)
    {
        value_ = value;
    }

    std::function<void(JsonObjectBuilder &)> ToJson() const
    {
        auto property = type_.ToJson();
        AddProperty(property, "name", name_);

        if (value_) {
            AddProperty(property, "value", *value_);
        }

        return property;
    }

private:
    std::string name_;
    RemoteObjectType type_;
    std::optional<std::string> value_;
};

}  // namespace ark::tooling::inspector

#endif  // PANDA_TOOLING_INSPECTOR_TYPES_PROPERTY_PREVIEW_H