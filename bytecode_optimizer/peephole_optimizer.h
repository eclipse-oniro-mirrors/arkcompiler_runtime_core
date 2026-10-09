/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#ifndef BYTECODE_OPTIMIZER_PEEPHOLE_OPTIMIZER_H
#define BYTECODE_OPTIMIZER_PEEPHOLE_OPTIMIZER_H

#include "compiler/optimizer/ir/graph.h"
#include "compiler/optimizer/ir/inst.h"
#include "compiler/optimizer/pass.h"

namespace panda::bytecodeopt {

class PeepholeOptimizer final : public compiler::Optimization {
public:
    explicit PeepholeOptimizer(compiler::Graph *graph) : Optimization(graph) {}
    NO_COPY_SEMANTIC(PeepholeOptimizer);
    NO_MOVE_SEMANTIC(PeepholeOptimizer);
    ~PeepholeOptimizer() override = default;

    bool RunImpl() override;

    void InvalidateAnalyses() override;

    const char *GetPassName() const override
    {
        return "PeepholeOptimizer";
    }

    bool IsEnable() const override
    {
        return true;
    }

private:
    using IntrinsicId = compiler::RuntimeInterface::IntrinsicId;

    enum class NullishConstant : uint8_t { NONE, NULL_VALUE, UNDEFINED_VALUE };

    struct EqualitySuccessors {
        compiler::BasicBlock *equal {nullptr};
        compiler::BasicBlock *not_equal {nullptr};
    };

    static bool IsTrueIntrinsic(IntrinsicId id);
    static bool IsFalseIntrinsic(IntrinsicId id);
    static bool IsToNumeric(IntrinsicId id);
    static bool IsStrictComparison(IntrinsicId id);
    static bool IsStrictNumericType(compiler::DataType::Type type);
    static size_t GetDataInputCount(const compiler::Inst *inst);
    static bool HasOnlyUser(const compiler::Inst *definition, const compiler::Inst *user);
    static NullishConstant GetNullishConstant(const compiler::Inst *inst);
    static bool GetNullishCompare(const compiler::Inst *inst, compiler::Inst **value, compiler::Inst **constant,
                                  NullishConstant *kind);
    static compiler::Inst *GetStrictCompareFromBranch(compiler::Inst *branch);
    static bool GetEqualitySuccessors(compiler::Inst *compare, compiler::Inst *branch, EqualitySuccessors *successors,
                                      compiler::Inst **condition);
    static bool HasSameEhRegion(const compiler::BasicBlock *first, const compiler::BasicBlock *second);
    static bool IsOnlyNullishCheckScaffolding(const compiler::BasicBlock *block, const compiler::Inst *constant,
                                              const compiler::Inst *compare, const compiler::Inst *condition,
                                              const compiler::Inst *branch);

    bool RewriteBoolean(compiler::Inst *inst);
    bool RewriteToNumeric(compiler::Inst *inst);
    bool RewriteNullishCheckChain(compiler::BasicBlock *block);
};

}  // namespace panda::bytecodeopt

#endif  // BYTECODE_OPTIMIZER_PEEPHOLE_OPTIMIZER_H
