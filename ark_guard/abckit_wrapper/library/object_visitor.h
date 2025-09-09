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

#ifndef ABCKIT_WRAPPER_OBJECT_VISITOR_H
#define ABCKIT_WRAPPER_OBJECT_VISITOR_H

#include <variant>
#include "abckit_wrapper/module.h"

namespace abckit_wrapper {
/**
 * @brief ObjectVisitor
 *
 * wrap object visitor for object visit
 */
class ObjectVisitor {
public:
    /**
     * @brief ObjectVisitor Constructor
     * @param visitor NamespaceVisitor
     */
    explicit ObjectVisitor(NamespaceVisitor *visitor) : visitor_(visitor) {}

    /**
     * @brief ObjectVisitor Constructor
     * @param visitor ClassVisitor
     */
    explicit ObjectVisitor(ClassVisitor *visitor) : visitor_(visitor) {}

    /**
     * @brief ObjectVisitor Constructor
     * @param childVisitor ChildVisitor
     */
    explicit ObjectVisitor(ChildVisitor *childVisitor) : childVisitor_(childVisitor) {}

    /**
     * @brief Visit namespace
     * @param ns visited namespace
     * @return `false` if was early exited. Otherwise - `true`.
     */
    bool operator()(Namespace *ns) const;

    /**
     * @brief Visit class
     * @param clazz visited class
     * @return `false` if was early exited. Otherwise - `true`.
     */
    bool operator()(Class *clazz) const;

    /**
     * Accept instance accept with instance visitor
     * @tparam InstanceType instance type
     * @tparam VisitorType instance visitor type
     * @param instance visited instance
     * @return `false` if was early exited. Otherwise - `true`.
     */
    template <typename InstanceType, typename VisitorType>
    bool Accept(InstanceType *instance) const
    {
        if (childVisitor_) {
            return instance->Accept(*childVisitor_);
        }

        if (auto visitor = std::get_if<VisitorType *>(&visitor_)) {
            return instance->Accept(**visitor);
        }

        return true;
    }

private:
    std::variant<NamespaceVisitor *, ClassVisitor *> visitor_;
    ChildVisitor *childVisitor_ = nullptr;
};
}  // namespace abckit_wrapper

#endif  // ABCKIT_WRAPPER_OBJECT_VISITOR_H
