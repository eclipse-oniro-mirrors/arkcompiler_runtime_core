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

#ifndef PANDA_RUNTIME_CLASS_LOCK_H
#define PANDA_RUNTIME_CLASS_LOCK_H

#include "runtime/include/runtime.h"
#include "runtime/include/object_header.h"
#include "runtime/include/mem/panda_containers.h"

namespace ark {

/// @brief The class manages per-class lock/unlock on construction/destruction
class ClassLock {
public:
    explicit ClassLock(const ObjectHeader *obj);

    bool Wait(bool ignoreInterruption = false);

    void Notify();

    void NotifyAll();

    ~ClassLock();

    NO_COPY_SEMANTIC(ClassLock);
    NO_MOVE_SEMANTIC(ClassLock);

private:
    Class *cls_ {nullptr};
    ClassMutexHandler *clsMtx_ {nullptr};
};
}  // namespace ark

#endif  // PANDA_RUNTIME_CLASS_LOCK_H
