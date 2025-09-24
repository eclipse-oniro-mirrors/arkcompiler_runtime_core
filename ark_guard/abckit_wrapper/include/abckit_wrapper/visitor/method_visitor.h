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

#ifndef ABCKIT_WRAPPER_METHOD_VISITOR_H
#define ABCKIT_WRAPPER_METHOD_VISITOR_H

namespace abckit_wrapper {

class Method;
class Parameter;

/**
 * @brief MethodVisitor
 */
class MethodVisitor {
public:
    /**
     * @brief MethodVisitor default Destructor
     */
    virtual ~MethodVisitor() = default;

    /**
     * @brief Visit method
     * @param method visited method
     * @return `false` if was early exited. Otherwise - `true`.
     */
    virtual bool Visit(Method *method) = 0;
};

/**
 * @brief ParameterVisitor
 */
class ParameterVisitor {
public:
    /**
     * @brief ParameterVisitor default Destructor
     */
    virtual ~ParameterVisitor() = default;

    /**
     * @brief Visit method parameter
     * @param parameter visited parameter
     * @return `false` if was early exited. Otherwise - `true`.
     */
    virtual bool Visit(Parameter *parameter) = 0;
};

}  // namespace abckit_wrapper

#endif  // ABCKIT_WRAPPER_METHOD_VISITOR_H
