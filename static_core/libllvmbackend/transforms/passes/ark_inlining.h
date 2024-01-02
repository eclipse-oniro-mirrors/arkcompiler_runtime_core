/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef LIBLLVMBACKEND_TRANSFORMS_PASSES_ARK_INLINING_H
#define LIBLLVMBACKEND_TRANSFORMS_PASSES_ARK_INLINING_H

#include <llvm/Transforms/IPO/Inliner.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/IR/PassManager.h>

namespace panda::llvmbackend {
struct LLVMCompilerOptions;
}  // namespace panda::llvmbackend

namespace panda::llvmbackend {
class LLVMArkInterface;
}  // namespace panda::llvmbackend

namespace panda::llvmbackend::passes {

class IrtocInlineChecker : public llvm::PassInfoMixin<IrtocInlineChecker> {
public:
    static constexpr llvm::StringRef ARG_NAME = "irtoc-inline-check";

    static bool ShouldInsert(const panda::llvmbackend::LLVMCompilerOptions *options);
    void CheckShouldInline(llvm::CallBase *call_base);

    // NOLINTNEXTLINE(readability-identifier-naming)
    llvm::PreservedAnalyses run(llvm::LazyCallGraph::SCC &component, llvm::CGSCCAnalysisManager & /*unused*/,
                                llvm::LazyCallGraph & /*unused*/, llvm::CGSCCUpdateResult & /*unused*/);
};

class InlinePrepare : public llvm::PassInfoMixin<InlinePrepare> {
    llvm::InlineParams inline_params_;

public:
    static constexpr llvm::StringRef ARG_NAME = "inline-prepare";

    explicit InlinePrepare(llvm::InlineParams inline_params) : inline_params_ {inline_params} {}
    static bool ShouldInsert(const panda::llvmbackend::LLVMCompilerOptions *options);
    static InlinePrepare Create(LLVMArkInterface *ark_interface,
                                const panda::llvmbackend::LLVMCompilerOptions *options);

    // NOLINTNEXTLINE(readability-identifier-naming)
    llvm::PreservedAnalyses run(llvm::Module &module, llvm::ModuleAnalysisManager &module_am);
};

}  // namespace panda::llvmbackend::passes

#endif  // LIBLLVMBACKEND_TRANSFORMS_PASSES_ARK_INLINING_H