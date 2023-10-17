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

#ifndef PANDA_MEM_GC_REFERENCE_PROCESSOR_REFERENCE_PROCESSOR_H
#define PANDA_MEM_GC_REFERENCE_PROCESSOR_REFERENCE_PROCESSOR_H

#include "libpandabase/macros.h"
#include "runtime/include/coretypes/tagged_value.h"
#include "runtime/include/mem/panda_containers.h"
#include "runtime/mem/gc/gc_root.h"
#include "runtime/mem/gc/gc.h"

namespace panda {
class ObjectHeader;
class BaseClass;
namespace mem {
enum class GCPhase;
class GC;
class Reference;
}  // namespace mem
}  // namespace panda

namespace panda::mem {

/// General language-independent interface for ReferenceProcessing.
class ReferenceProcessor {
public:
    using ReferenceCheckPredicateT = typename GC::ReferenceCheckPredicateT;
    using ReferenceProcessPredicateT = typename GC::ReferenceProcessPredicateT;

    explicit ReferenceProcessor() = default;
    NO_COPY_SEMANTIC(ReferenceProcessor);
    NO_MOVE_SEMANTIC(ReferenceProcessor);
    virtual ~ReferenceProcessor() = default;

    /**
     * True if current object is Reference and it's referent is not marked yet (maybe need to process this reference)
     * Predicate checks GC-specific conditions on this reference (i.e. if we need to skip this reference e.g. referent
     * is not in collection set)
     */
    virtual bool IsReference(const BaseClass *base_cls, const ObjectHeader *ref,
                             const ReferenceCheckPredicateT &pred) const = 0;

    /**
     * Save reference for future processing and handle it with GC point of view (mark needed fields, if necessary)
     * Predicate checks if we should add this reference to the queue (e.g. don't process to many refs on concurrent)
     */
    virtual void HandleReference(GC *gc, GCMarkingStackType *objects_stack, const BaseClass *cls,
                                 const ObjectHeader *object, const ReferenceProcessPredicateT &pred) = 0;

    /**
     * Process all references which we discovered by GC.
     * Predicate checks if we should process all references at once (e.g. processing takes too much time)
     */
    virtual void ProcessReferences(bool concurrent, bool clear_soft_references, GCPhase gc_phase,
                                   const mem::GC::ReferenceClearPredicateT &pred) = 0;

    /// Collect all processed references. They were cleared on the previous phase - we only collect them.
    virtual panda::mem::Reference *CollectClearedReferences() = 0;

    virtual void ScheduleForEnqueue(Reference *cleared_references) = 0;

    /// Enqueue cleared references to corresponding queue, if necessary.
    virtual void Enqueue(panda::mem::Reference *cleared_references) = 0;

    /// Return size of the queue of references.
    virtual size_t GetReferenceQueueSize() const = 0;
};

}  // namespace panda::mem
#endif  // PANDA_MEM_GC_REFERENCE_PROCESSOR_REFERENCE_PROCESSOR_H
