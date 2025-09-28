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

#ifndef ABCKIT_WRAPPER_METHOD_H
#define ABCKIT_WRAPPER_METHOD_H

#include "libabckit/cpp/abckit_cpp.h"
#include "member.h"
#include "visitor.h"

namespace abckit_wrapper {

/**
 * @brief Parameter
 */
class Parameter final : public Object, public SingleImplObject<abckit::core::FunctionParam> {
public:
    /**
     * @brief Parameter Constructor
     * @param param abckit::core::FunctionParam
     */
    explicit Parameter(abckit::core::FunctionParam param) : SingleImplObject(std::move(param)) {}

    std::string GetName() const override;

    bool SetName(const std::string &name) override;

    /**
     * @brief Get parameter type name
     * @return parameter type name
     */
    [[nodiscard]] std::string GetTypeName() const;

    /**
     * @brief Accept visit
     * @param visitor ParameterVisitor
     * @return `false` if was early exited. Otherwise - `true`.
     */
    bool Accept(ParameterVisitor &visitor);

    /**
     * parameter owing method
     */
    std::optional<Method *> owingMethod_;
};

/**
 * @brief Method
 *
 * function(owing to module) or method(owing to class)
 */
class Method final : public Member, public SingleImplObject<abckit::core::Function> {
public:
    /**
     * @brief Method Constructor
     * @param function abckit::core::Function
     */
    explicit Method(abckit::core::Function function) : SingleImplObject(std::move(function)) {}

    AbckitWrapperErrorCode Init() override;

    std::string GetName() const override;

    bool SetName(const std::string &name) override;

    /**
     * @brief Tells if Method is constructor
     * @return `true` if Method is constructor
     */
    bool IsConstructor() const;

    /**
     * @brief Accept visit
     * @param visitor MethodVisitor
     * @return `false` if was early exited. Otherwise - `true`.
     */
    bool Accept(MethodVisitor &visitor);

    /**
     * @brief Children accept visit
     * @param visitor ChildVisitor
     * @return `false` if was early exited. Otherwise - `true`.
     */
    bool Accept(ChildVisitor &visitor);

    /**
     * @brief Parameters accept visit
     * @param visitor ParameterVisitor
     * @return `false` if was early exited. Otherwise - `true`.
     */
    bool ParametersAccept(ParameterVisitor &visitor) const;

private:
    AbckitWrapperErrorCode InitSignature() override;

    void InitAccessFlags() override;

    void InitAnnotation(Annotation *annotation) override;

    std::vector<abckit::core::Annotation> GetAnnotations() const override;

    AbckitWrapperErrorCode InitParameters();

    std::vector<std::unique_ptr<Parameter>> parameters_;
};
}  // namespace abckit_wrapper

#endif  // ABCKIT_WRAPPER_METHOD_H
