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

#include "remove_unused_functions.h"
#include "llvm_ark_interface.h"
#include "inline_ir_utils.h"
#include "llvm_compiler_options.h"

#include <llvm/Pass.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/IPO/FunctionImport.h>

#define DEBUG_TYPE "remove-unused-functions"

using llvm::Argument;
using llvm::BasicBlock;
using llvm::cast;
using llvm::convertToDeclaration;
using llvm::DenseSet;
using llvm::Function;
using llvm::InlineAsm;
using llvm::isa;
using llvm::User;
using llvm::Value;

namespace panda::llvmbackend::passes {

bool RemoveUnusedFunctions::ShouldInsert(const panda::llvmbackend::LLVMCompilerOptions *options)
{
    return options->do_irtoc_inline;
}

llvm::PreservedAnalyses RemoveUnusedFunctions::run(llvm::Module &module, llvm::ModuleAnalysisManager & /*AM*/)
{
    DenseSet<Function *> used_functions;
    for (auto &function : module.functions()) {
        if (function.getMetadata(LLVMArkInterface::FUNCTION_MD_INLINE_MODULE) != nullptr) {
            LLVM_DEBUG(llvm::dbgs() << "Skip " << function.getName() << " from inline module\n");
            continue;
        }
        if (function.isDeclaration()) {
            continue;
        }
        LLVM_DEBUG(llvm::dbgs() << function.getName() << " is root\n");
        DenseSet<Value *> seen;
        VisitValue(used_functions, function, seen);
    }

    bool changed = false;
    for (auto &function : module.functions()) {
        if (!used_functions.contains(&function)) {
            LLVM_DEBUG(llvm::dbgs() << "Deleted body of " << function.getName() << "\n");
            convertToDeclaration(function);
            changed |= true;
        }
    }

    changed |= panda::llvmbackend::RemoveDanglingAliases(module);
    return changed ? llvm::PreservedAnalyses::none() : llvm::PreservedAnalyses::all();
}
void RemoveUnusedFunctions::VisitValue(DenseSet<Function *> &used_functions, Value &value,
                                       DenseSet<Value *> &seen_values)
{
    if (seen_values.contains(&value)) {
        return;
    }

    seen_values.insert(&value);

    if (isa<Function>(value)) {
        auto &function = cast<Function>(value);
        if (used_functions.contains(&function)) {
            return;
        }

        used_functions.insert(&function);
        DenseSet<Value *> seen;
        for (auto &basic_block : function) {
            VisitValue(used_functions, basic_block, seen);
        }
    } else if (isa<BasicBlock>(value)) {
        auto &basic_block = cast<BasicBlock>(value);
        for (auto &instruction : basic_block) {
            VisitValue(used_functions, instruction, seen_values);
        }
    } else if (isa<User>(value)) {
        auto &user = cast<User>(value);
        for (auto operand : user.operand_values()) {
            VisitValue(used_functions, *operand, seen_values);
        }
    } else {
        if (isa<Argument, llvm::MetadataAsValue, InlineAsm>(value)) {
            return;
        }
        LLVM_DEBUG(llvm::dbgs() << "Value = ");
        LLVM_DEBUG(value.print(llvm::dbgs()));
        LLVM_DEBUG(llvm::dbgs() << "\n");
        llvm_unreachable("Unexpected value");
    }
}

}  // namespace panda::llvmbackend::passes
