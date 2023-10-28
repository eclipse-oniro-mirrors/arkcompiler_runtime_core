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

#ifndef LIBLLVMBACKEND_LOWERING_GC_BARRIERS_H
#define LIBLLVMBACKEND_LOWERING_GC_BARRIERS_H

#include <llvm/IR/IRBuilder.h>

namespace panda::llvmbackend {
class LLVMArkInterface;
}  // namespace panda::llvmbackend

namespace panda::llvmbackend::gc_barriers {
void EmitPreWRB(llvm::IRBuilder<> *builder, llvm::Value *mem, bool is_volatile_mem, llvm::BasicBlock *out_bb,
                LLVMArkInterface *ark_interface, llvm::Value *thread_reg_value);

void EmitPostWRB(llvm::IRBuilder<> *builder, llvm::Value *mem, llvm::Value *offset, llvm::Value *value,
                 LLVMArkInterface *ark_interface, llvm::Value *thread_reg_value, llvm::Value *frame_reg_value);
}  // namespace panda::llvmbackend::gc_barriers

#endif  // LIBLLVMBACKEND_LOWERING_GC_BARRIERS_H
