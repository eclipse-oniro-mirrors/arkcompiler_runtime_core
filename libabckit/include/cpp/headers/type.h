/*
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

#ifndef CPP_ABCKIT_TYPE_H
#define CPP_ABCKIT_TYPE_H

#include "base_classes.h"
#include "core/class.h"

namespace abckit {

/**
 * @brief Type
 */
class Type : public ViewInResource<AbckitType *, const File *> {
    /// @brief abckit::File
    friend class abckit::File;
    /// @brief abckit::File
    friend class abckit::core::AnnotationInterfaceField;
    /// @brief abckit::Value
    friend class abckit::Value;
    /// @brief arkts::AnnotationInterface
    friend class arkts::AnnotationInterface;
    /// @brief abckit::Instruction
    friend class abckit::Instruction;

public:
    /**
     * @brief Construct a new Type object
     * @param other
     */
    Type(const Type &other) = default;

    /**
     * @brief Constructor
     * @param other
     * @return Type&
     */
    Type &operator=(const Type &other) = default;

    /**
     * @brief Construct a new Type object
     * @param other
     */
    Type(Type &&other) = default;

    /**
     * @brief Constructor
     * @param other
     * @return Type&
     */
    Type &operator=(Type &&other) = default;

    /**
     * @brief Destroy the Type object
     */
    ~Type() override = default;

    /**
     * @brief Returns the Type Id of type
     * @return Returns the AbckitTypeId
     */
    inline enum AbckitTypeId GetTypeId() const
    {
        auto ret = GetApiConfig()->cIapi_->typeGetTypeId(GetView());
        CheckError(GetApiConfig());
        return ret;
    }

    /**
     * @brief Returns instance of a `core::Class` that the type is reference to.
     * @return Instance of a `core::Class` that the type references.
     */
    inline core::Class GetReferenceClass() const
    {
        auto *ret = GetApiConfig()->cIapi_->typeGetReferenceClass(GetView());
        CheckError(GetApiConfig());
        return core::Class(ret, GetApiConfig(), GetResource());
    }

protected:
    /**
     * @brief Get the Api Config object
     * @return const ApiConfig*
     */
    const ApiConfig *GetApiConfig() const override
    {
        return conf_;
    }

private:
    explicit Type(AbckitType *type, const ApiConfig *conf, const File *file) : ViewInResource(type), conf_(conf)
    {
        SetResource(file);
    };
    const ApiConfig *conf_;
};

}  // namespace abckit

#endif  // CPP_ABCKIT_TYPE_H
