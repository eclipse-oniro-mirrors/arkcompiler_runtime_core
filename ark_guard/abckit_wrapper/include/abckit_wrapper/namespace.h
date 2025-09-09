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

#ifndef ABCKIT_WRAPPER_NAMESPACE_H
#define ABCKIT_WRAPPER_NAMESPACE_H

#include "base_module.h"
#include "visitor.h"

namespace abckit_wrapper {
/**
 * @brief Namespace
 */
class Namespace final : public BaseModule {
public:
    /**
     * Namespace Constructor
     * @param ns abckit::core::Namespace
     */
    explicit Namespace(abckit::core::Namespace ns) : BaseModule(std::move(ns)) {}

    /**
     * @brief Accept visit
     * @param visitor NamespaceVisitor
     * @return `false` if was early exited. Otherwise - `true`.
     */
    bool Accept(NamespaceVisitor &visitor);

    /**
     * @brief Accept ChildVisitor visit
     * @param visitor ChildVisitor
     * @return `false` if was early exited. Otherwise - `true`.
     */
    bool Accept(ChildVisitor &visitor);

private:
    void InitForObject(Object *object) override;
};
}  // namespace abckit_wrapper

#endif  // ABCKIT_WRAPPER_NAMESPACE_H
