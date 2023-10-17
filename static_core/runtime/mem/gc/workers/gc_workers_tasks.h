/**
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

#ifndef PANDA_RUNTIME_MEM_GC_GC_WORKERS_TASKS_H
#define PANDA_RUNTIME_MEM_GC_GC_WORKERS_TASKS_H

#include "runtime/mem/gc/g1/ref_updater.h"
#include "runtime/mem/gc/gc_root.h"
#include "runtime/thread_pool_queue.h"
#include "libpandabase/utils/range.h"

namespace panda::mem {

enum class GCWorkersTaskTypes : uint32_t {
    TASK_EMPTY,
    TASK_MARKING,
    TASK_REMARK,
    TASK_FULL_MARK,
    TASK_REGION_COMPACTING,
    TASK_RETURN_FREE_PAGES_TO_OS,
    TASK_UPDATE_REMSET_REFS,
    TASK_ENQUEUE_REMSET_REFS,
};

constexpr const char *GCWorkersTaskTypesToString(GCWorkersTaskTypes type)
{
    switch (type) {
        case GCWorkersTaskTypes::TASK_EMPTY:
            return "Empty task";
        case GCWorkersTaskTypes::TASK_MARKING:
            return "Marking task";
        case GCWorkersTaskTypes::TASK_REMARK:
            return "Remark task";
        case GCWorkersTaskTypes::TASK_FULL_MARK:
            return "Marking task for full collection";
        case GCWorkersTaskTypes::TASK_REGION_COMPACTING:
            return "Region compacting task";
        case GCWorkersTaskTypes::TASK_RETURN_FREE_PAGES_TO_OS:
            return "Return free pages to the OS";
        case GCWorkersTaskTypes::TASK_UPDATE_REMSET_REFS:
            return "Update remset references task";
        case GCWorkersTaskTypes::TASK_ENQUEUE_REMSET_REFS:
            return "Enqueue remset references task";
        default:
            return "Unknown task";
    }
}

class GCWorkersTask : public TaskInterface {
public:
    explicit GCWorkersTask(GCWorkersTaskTypes type = GCWorkersTaskTypes::TASK_EMPTY) : task_type_(type)
    {
        ASSERT(type == GCWorkersTaskTypes::TASK_EMPTY || type == GCWorkersTaskTypes::TASK_RETURN_FREE_PAGES_TO_OS);
    }

    ~GCWorkersTask() = default;
    DEFAULT_COPY_SEMANTIC(GCWorkersTask);
    DEFAULT_MOVE_SEMANTIC(GCWorkersTask);

    bool IsEmpty() const
    {
        return task_type_ == GCWorkersTaskTypes::TASK_EMPTY;
    }

    template <class GCWorkersTaskT>
    std::enable_if_t<std::is_base_of_v<GCWorkersTask, GCWorkersTaskT>, GCWorkersTaskT *> Cast() const
    {
        return static_cast<GCWorkersTaskT *>(const_cast<GCWorkersTask *>(this));
    }

    template <class GCWorkersTaskT>
    std::enable_if_t<!std::is_base_of_v<GCWorkersTask, GCWorkersTaskT>, GCWorkersTaskT *> Cast() const = delete;

    GCWorkersTaskTypes GetType() const
    {
        return task_type_;
    }

private:
    GCWorkersTaskTypes task_type_;

protected:
    using StorageType = void *;

    GCWorkersTask(GCWorkersTaskTypes type, StorageType task_storage_data)
        : task_type_(type), storage_(task_storage_data)
    {
        ASSERT(storage_ != nullptr);
    }

    StorageType storage_ {nullptr};  // NOLINT(misc-non-private-member-variables-in-classes)
};

class GCMarkWorkersTask : public GCWorkersTask {
public:
    using StackType = GCMarkingStackType;
    GCMarkWorkersTask(GCWorkersTaskTypes type, StackType *marking_stack) : GCWorkersTask(type, marking_stack)
    {
        ASSERT(type == GCWorkersTaskTypes::TASK_MARKING || type == GCWorkersTaskTypes::TASK_REMARK ||
               type == GCWorkersTaskTypes::TASK_FULL_MARK);
    }
    DEFAULT_COPY_SEMANTIC(GCMarkWorkersTask);
    DEFAULT_MOVE_SEMANTIC(GCMarkWorkersTask);
    ~GCMarkWorkersTask() = default;

    StackType *GetMarkingStack() const
    {
        return static_cast<StackType *>(storage_);
    }
};

class Region;

class GCRegionCompactWorkersTask : public GCWorkersTask {
public:
    using RegionDataType = std::pair<Region *, ObjectVisitor>;

    explicit GCRegionCompactWorkersTask(RegionDataType *region_data)
        : GCWorkersTask(GCWorkersTaskTypes::TASK_REGION_COMPACTING, region_data)
    {
    }
    DEFAULT_COPY_SEMANTIC(GCRegionCompactWorkersTask);
    DEFAULT_MOVE_SEMANTIC(GCRegionCompactWorkersTask);
    ~GCRegionCompactWorkersTask() = default;

    RegionDataType *GetRegionData() const
    {
        return static_cast<RegionDataType *>(storage_);
    }
};

template <bool VECTOR>
class GCUpdateRefsWorkersTask : public GCWorkersTask {
public:
    using MovedObjectsRange = std::conditional_t<VECTOR, Range<PandaVector<ObjectHeader *>::iterator>,
                                                 Range<PandaDeque<ObjectHeader *>::iterator>>;
    // We need this to evenly split moved objects vector to ranges for gc workers
    static constexpr int RANGE_SIZE = 4096;

    explicit GCUpdateRefsWorkersTask(MovedObjectsRange *moved_objects)
        : GCWorkersTask(GCWorkersTaskTypes::TASK_ENQUEUE_REMSET_REFS, moved_objects)
    {
    }
    DEFAULT_COPY_SEMANTIC(GCUpdateRefsWorkersTask);
    DEFAULT_MOVE_SEMANTIC(GCUpdateRefsWorkersTask);
    ~GCUpdateRefsWorkersTask() = default;

    MovedObjectsRange *GetMovedObjectsRange() const
    {
        return static_cast<MovedObjectsRange *>(storage_);
    }
};

}  // namespace panda::mem

#endif  // PANDA_RUNTIME_MEM_GC_GC_WORKERS_TASKS_H
