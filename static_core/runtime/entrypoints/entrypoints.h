/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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
#ifndef PANDA_RUNTIME_ENTRYPOINTS_ENTRYPOINTS_H
#define PANDA_RUNTIME_ENTRYPOINTS_ENTRYPOINTS_H

#include "entrypoints_gen.h"
#include "plugins_entrypoints_gen.h"
#include "runtime/include/thread.h"

namespace panda {

class Frame;

extern "C" Frame *CreateFrameWithSize(uint32_t size, uint32_t nregs, Method *method, Frame *prev);

extern "C" Frame *CreateFrameWithActualArgsAndSize(uint32_t size, uint32_t nregs, uint32_t numActualArgs,
                                                   Method *method, Frame *prev);

extern "C" PANDA_PUBLIC_API Frame *CreateNativeFrameWithActualArgsAndSize(uint32_t size, uint32_t nregs,
                                                                          uint32_t numActualArgs, Method *method,
                                                                          Frame *prev);

extern "C" Frame *CreateFrameForMethod(Method *method, Frame *prev);

extern "C" Frame *CreateFrameForMethodDyn(Method *method, Frame *prev);

extern "C" Frame *CreateFrameForMethodWithActualArgs(uint32_t numActualArgs, Method *method, Frame *prev);

extern "C" Frame *CreateFrameForMethodWithActualArgsDyn(uint32_t numActualArgs, Method *method, Frame *prev);

extern "C" PANDA_PUBLIC_API void FreeFrame(Frame *frame);

extern "C" void FreeFrameInterp(Frame *frame, ManagedThread *current);

extern "C" void ThrowInstantiationErrorEntrypoint(Class *klass);

}  // namespace panda

#endif  // PANDA_RUNTIME_ENTRYPOINTS_ENTRYPOINTS_H
