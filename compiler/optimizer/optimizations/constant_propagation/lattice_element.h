/*
 * Copyright (c) 2024 Shenzhen Kaihong Digital Industry Development Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef COMPILER_OPTIMIZER_OPTIMIZATIONS_LATTICE_H
#define COMPILER_OPTIMIZER_OPTIMIZATIONS_LATTICE_H

#include <variant>
#include "compiler/optimizer/ir/basicblock.h"
#include "compiler/optimizer/ir/graph.h"
#include "compiler/optimizer/ir/graph_visitor.h"
#include "compiler/optimizer/pass.h"
#include "utils/hash.h"

namespace panda::compiler {
class ConstantElement;
class LatticeElement {
public:
    enum class LatticeType {
        LATTICE_TOP,
        LATTICE_CONSTANT,
        LATTICE_BOTTOM,
    };

    explicit LatticeElement(LatticeType type);
    virtual ~LatticeElement() = default;
    NO_MOVE_SEMANTIC(LatticeElement);
    NO_COPY_SEMANTIC(LatticeElement);

    bool IsConstantElement()
    {
        return type_ == LatticeType::LATTICE_CONSTANT;
    }

    bool IsTopElement()
    {
        return type_ == LatticeType::LATTICE_TOP;
    }

    bool IsBottomElement()
    {
        return type_ == LatticeType::LATTICE_BOTTOM;
    }

    virtual ConstantElement *AsConstant()
    {
        return nullptr;
    }

    virtual LatticeElement *Meet(LatticeElement *other) = 0;
    virtual std::string ToString() = 0;

private:
    LatticeType type_;
};

class TopElement : public LatticeElement {
public:
    NO_MOVE_SEMANTIC(TopElement);
    NO_COPY_SEMANTIC(TopElement);
    static LatticeElement *GetInstance();
    ~TopElement() override = default;
    LatticeElement *Meet(LatticeElement *other) override;
    std::string ToString() override;

protected:
    TopElement();
};

class BottomElement : public LatticeElement {
public:
    NO_MOVE_SEMANTIC(BottomElement);
    NO_COPY_SEMANTIC(BottomElement);
    ~BottomElement() override = default;
    LatticeElement *Meet(LatticeElement *other) override;
    std::string ToString() override;
    static LatticeElement *GetInstance();

protected:
    BottomElement();
};

class ConstantElement : public LatticeElement {
public:
    enum ConstantType { CONSTANT_BOOL, CONSTANT_INT32, CONSTANT_INT64, CONSTANT_DOUBLE };

    NO_MOVE_SEMANTIC(ConstantElement);
    NO_COPY_SEMANTIC(ConstantElement);
    explicit ConstantElement(bool val);
    explicit ConstantElement(int32_t val);
    explicit ConstantElement(int64_t val);
    explicit ConstantElement(double val);
    ~ConstantElement() override = default;
    LatticeElement *Meet(LatticeElement *other) override;
    std::string ToString() override;
    ConstantElement *AsConstant() override;

    auto &GetVal() const
    {
        return val_;
    }

    ConstantType GetType() const
    {
        return type_;
    }

private:
    ConstantType type_;
    std::variant<bool, int32_t, int64_t, double> val_;
};
}  // namespace panda::compiler

#endif
