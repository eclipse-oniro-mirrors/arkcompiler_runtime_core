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

#include "libpandabase/mem/space.h"
#include "runtime/include/language_config.h"
#include "runtime/include/class.h"
#include "runtime/include/mem/panda_string.h"
#include "runtime/include/panda_vm.h"
#include "runtime/mem/gc/card_table-inl.h"
#include "runtime/mem/gc/dynamic/gc_marker_dynamic-inl.h"
#include "runtime/mem/gc/gc.h"
#include "runtime/mem/gc/g1/g1-gc.h"
#include "runtime/mem/gc/g1/g1-helpers.h"
#include "runtime/mem/gc/g1/ref_cache_builder.h"
#include "runtime/mem/gc/g1/update_remset_task_queue.h"
#include "runtime/mem/gc/g1/update_remset_thread.h"
#include "runtime/mem/gc/workers/gc_workers_task_pool.h"
#include "runtime/mem/gc/generational-gc-base-inl.h"
#include "runtime/mem/gc/static/gc_marker_static-inl.h"
#include "runtime/mem/gc/reference-processor/reference_processor.h"
#include "runtime/mem/object_helpers-inl.h"
#include "runtime/mem/rem_set-inl.h"
#include "runtime/include/thread.h"
#include "runtime/include/managed_thread.h"
#include "runtime/mem/gc/g1/ref_updater.h"
#include "runtime/mem/region_space.h"

namespace panda::mem {

#ifndef NDEBUG
static bool IsCardTableClear(CardTable *cardTable)
{
    bool clear = true;
    cardTable->VisitMarked(
        [&clear](const MemRange &range) {
            LOG(ERROR, GC) << "Card [" << ToVoidPtr(range.GetStartAddress()) << " - "
                           << ToVoidPtr(range.GetEndAddress()) << "] is not clear";
            clear = false;
        },
        CardTableProcessedFlag::VISIT_MARKED | CardTableProcessedFlag::VISIT_PROCESSED);
    return clear;
}
#endif

/* static */
template <class LanguageConfig>
void G1GC<LanguageConfig>::CalcLiveBytesMarkPreprocess(const ObjectHeader *object, BaseClass *baseKlass)
{
    Region *region = ObjectToRegion(object);
    size_t objectSize = GetAlignedObjectSize(object->ObjectSize<LanguageConfig::LANG_TYPE>(baseKlass));
    region->AddLiveBytes<true>(objectSize);
}

/* static */
template <class LanguageConfig>
void G1GC<LanguageConfig>::CalcLiveBytesNotAtomicallyMarkPreprocess(const ObjectHeader *object, BaseClass *baseKlass)
{
    Region *region = ObjectToRegion(object);
    size_t objectSize = GetAlignedObjectSize(object->ObjectSize<LanguageConfig::LANG_TYPE>(baseKlass));
    region->AddLiveBytes<false>(objectSize);
}

template <class LanguageConfig>
G1GC<LanguageConfig>::G1GC(ObjectAllocatorBase *objectAllocator, const GCSettings &settings)
    : GenerationalGC<LanguageConfig>(objectAllocator, settings),
      marker_(this),
      concMarker_(this),
      mixedMarker_(this),
      concurrentMarkingStack_(this),
      numberOfMixedTenuredRegions_(settings.GetG1NumberOfTenuredRegionsAtMixedCollection()),
      regionGarbageRateThreshold_(settings.G1RegionGarbageRateThreshold()),
      g1PromotionRegionAliveRate_(settings.G1PromotionRegionAliveRate()),
      g1TrackFreedObjects_(settings.G1TrackFreedObjects()),
      isExplicitConcurrentGcEnabled_(settings.IsExplicitConcurrentGcEnabled()),
      regionSizeBits_(panda::helpers::math::GetIntLog2(this->GetG1ObjectAllocator()->GetRegionSize())),
      g1PauseTracker_(settings.GetG1GcPauseIntervalInMillis(), settings.GetG1MaxGcPauseInMillis()),
      analytics_(panda::time::GetCurrentTimeInNanos())
{
    InternalAllocatorPtr allocator = this->GetInternalAllocator();
    this->SetType(GCType::G1_GC);
    this->SetTLABsSupported();
    updatedRefsQueue_ = allocator->New<GCG1BarrierSet::ThreadLocalCardQueues>();
    auto *firstRefVector = allocator->New<RefVector>();
    firstRefVector->reserve(MAX_REFS);
    uniqueRefsFromRemsets_.push_back(firstRefVector);
    GetG1ObjectAllocator()->ReserveRegionIfNeeded();
}

template <class LanguageConfig>
G1GC<LanguageConfig>::~G1GC()
{
    InternalAllocatorPtr allocator = this->GetInternalAllocator();
    {
        for (auto objVector : satbBuffList_) {
            allocator->Delete(objVector);
        }
    }
    allocator->Delete(updatedRefsQueue_);
    ASSERT(uniqueRefsFromRemsets_.size() == 1);
    allocator->Delete(uniqueRefsFromRemsets_.front());
    uniqueRefsFromRemsets_.clear();
    this->GetInternalAllocator()->Delete(updateRemsetWorker_);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::InitGCBits(panda::ObjectHeader *objHeader)
{
    // The mutator may create a new object during concurrent marking phase.
    // In this case GC may don't mark it (for example only vregs may contain reference to the new object)
    // and collect. To avoid such situations add objects to a special buffer which
    // will be processed at remark stage.
    if (this->GetCardTable()->GetCardPtr(ToUintPtr(objHeader))->IsYoung() ||
        // Atomic with acquire order reason: read variable modified in GC thread
        !concurrentMarkingFlag_.load(std::memory_order_acquire)) {
        return;
    }
    os::memory::LockHolder lock(satbAndNewobjBufLock_);
    newobjBuffer_.push_back(objHeader);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::PreStartupImp()
{
    GenerationalGC<LanguageConfig>::DisableTenuredGC();
}

template <class LanguageConfig>
template <RegionFlag REGION_TYPE, bool FULL_GC>
void G1GC<LanguageConfig>::DoRegionCompacting(Region *region, bool useGcWorkers,
                                              PandaVector<PandaVector<ObjectHeader *> *> *movedObjectsVector)
{
    auto internalAllocator = this->GetInternalAllocator();
    ObjectVisitor movedObjectSaver;
    if constexpr (FULL_GC) {
        PandaVector<ObjectHeader *> *movedObjects;
        if (useGcWorkers) {
            movedObjects = internalAllocator->template New<PandaVector<ObjectHeader *>>();
            movedObjectsVector->push_back(movedObjects);
            size_t moveSize = region->GetAllocatedBytes();
            movedObjects->reserve(moveSize / GetMinimalObjectSize());
        } else {
            ASSERT(movedObjectsVector->size() == 1);
            movedObjects = movedObjectsVector->back();
        }
        movedObjectSaver = [movedObjects](ObjectHeader *object) { movedObjects->push_back(object); };
    } else {
        movedObjectSaver = []([[maybe_unused]] const ObjectHeader *object) {};
    }

    if (useGcWorkers) {
        auto *storage =
            internalAllocator->template New<GCRegionCompactWorkersTask::RegionDataType>(region, movedObjectSaver);
        if (!this->GetWorkersTaskPool()->AddTask(GCRegionCompactWorkersTask(storage))) {
            // We couldn't send a task to workers. Therefore, do it here.
            internalAllocator->Delete(storage);
            RegionCompactingImpl<true, REGION_TYPE>(region, movedObjectSaver);
        }
    } else {
        RegionCompactingImpl<false, REGION_TYPE>(region, movedObjectSaver);
    }
}

class ScopedRegionCollectionInfo {
public:
    ScopedRegionCollectionInfo(const GC *gc, const char *title, const Region *region, bool isYoung,
                               const size_t &movedSize)
        : gc_(gc),
          title_(title),
          region_(region),
          isYoung_(isYoung),
          movedSize_(movedSize),
          startTimeNs_(time::GetCurrentTimeInNanos())
    {
    }

    NO_COPY_SEMANTIC(ScopedRegionCollectionInfo);
    NO_MOVE_SEMANTIC(ScopedRegionCollectionInfo);

    ~ScopedRegionCollectionInfo()
    {
        if (gc_->IsLogDetailedGcCompactionInfoEnabled()) {
            LOG(INFO, GC) << *this;
        }
    }

private:
    const GC *gc_;
    const char *title_;
    const Region *region_;
    bool isYoung_;
    const size_t &movedSize_;
    uint64_t startTimeNs_;

    friend std::ostream &operator<<(std::ostream &log, const ScopedRegionCollectionInfo &regionInfo)
    {
        auto region = regionInfo.region_;
        log << '[' << regionInfo.gc_->GetCounter() << "] " << regionInfo.title_ << ": ";
        // Need to use saved is_young_ flag since region flags can be changed during region promotion
        if (regionInfo.isYoung_) {
            log << 'Y';
        } else {
            log << 'T';
        }
        DumpRegionRange(log, *region) << " A " << panda::helpers::MemoryConverter(region->GetAllocatedBytes()) << " L ";
        if (regionInfo.isYoung_) {
            log << '-';
        } else {
            log << panda::helpers::MemoryConverter(region->GetLiveBytes());
        }
        log << " RS " << region->GetRemSetSize() << " M " << panda::helpers::MemoryConverter(regionInfo.movedSize_)
            << " D " << panda::helpers::TimeConverter(time::GetCurrentTimeInNanos() - regionInfo.startTimeNs_);
        return log;
    }
};

template <class LanguageConfig>
template <bool ATOMIC>
void G1GC<LanguageConfig>::RegionPromotionImpl(Region *region, const ObjectVisitor &movedObjectSaver)
{
    size_t moveSize = region->GetAllocatedBytes();
    size_t aliveMoveCount = 0;
    size_t deadMoveCount = 0;
    auto objectAllocator = this->GetG1ObjectAllocator();
    auto promotionMoveChecker = [&aliveMoveCount, &movedObjectSaver](ObjectHeader *src) {
        ++aliveMoveCount;
        LOG_DEBUG_OBJECT_EVENTS << "PROMOTE YOUNG object " << src;
        ASSERT(ObjectToRegion(src)->HasFlag(RegionFlag::IS_EDEN));
        movedObjectSaver(src);
    };
    auto promotionDeathChecker = [this, &deadMoveCount](ObjectHeader *objectHeader) {
        if (IsMarked(objectHeader)) {
            return ObjectStatus::ALIVE_OBJECT;
        }
        ++deadMoveCount;
        LOG_DEBUG_OBJECT_EVENTS << "PROMOTE DEAD YOUNG object " << objectHeader;
        return ObjectStatus::DEAD_OBJECT;
    };
    ScopedRegionCollectionInfo collectionInfo(this, "Region promoted", region, true, moveSize);
    if (g1TrackFreedObjects_) {
        // We want to track all moved objects (including), therefore, iterate over all objects in region.
        objectAllocator->template PromoteYoungRegion<false>(region, promotionDeathChecker, promotionMoveChecker);
    } else {
        objectAllocator->template PromoteYoungRegion<true>(region, promotionDeathChecker, promotionMoveChecker);
        ASSERT(deadMoveCount == 0);
    }
    region->RmvFlag(RegionFlag::IS_COLLECTION_SET);
    this->memStats_.template RecordSizeMovedYoung<ATOMIC>(moveSize);
    this->memStats_.template RecordCountMovedYoung<ATOMIC>(aliveMoveCount + deadMoveCount);
    analytics_.ReportPromotedRegion();
    analytics_.ReportLiveObjects(aliveMoveCount);
}

template <class LanguageConfig>
template <typename Handler>
void G1GC<LanguageConfig>::IterateOverRefsInMemRange(const MemRange &memRange, Region *region, Handler &refsHandler)
{
    MarkBitmap *bitmap = nullptr;
    if (region->IsEden()) {
        ASSERT(this->IsFullGC());
        bitmap = region->GetMarkBitmap();
    } else {
        bitmap = region->GetLiveBitmap();
    }
    auto *startAddress = ToVoidPtr(memRange.GetStartAddress());
    auto *endAddress = ToVoidPtr(memRange.GetEndAddress());
    auto visitor = [&refsHandler, startAddress, endAddress](void *mem) {
        ObjectHelpers<LanguageConfig::LANG_TYPE>::template TraverseAllObjectsWithInfo<false>(
            static_cast<ObjectHeader *>(mem), refsHandler, startAddress, endAddress);
    };
    if (region->HasFlag(RegionFlag::IS_LARGE_OBJECT)) {
        bitmap->CallForMarkedChunkInHumongousRegion<false>(ToVoidPtr(region->Begin()), visitor);
    } else {
        bitmap->IterateOverMarkedChunkInRange(startAddress, endAddress, visitor);
    }
}

template <class LanguageConfig, bool CONCURRENTLY, bool COLLECT_CLASSES>
class NonRegularObjectsDeathChecker {
public:
    NonRegularObjectsDeathChecker(size_t *deleteSize, size_t *deleteCount)
        : deleteSize_(deleteSize), deleteCount_(deleteCount)
    {
    }

    ~NonRegularObjectsDeathChecker() = default;

    ObjectStatus operator()(ObjectHeader *objectHeader)
    {
        // NOLINTNEXTLINE(readability-braces-around-statements, bugprone-suspicious-semicolon)
        if constexpr (CONCURRENTLY) {
            // We may face a newly created object without live bitmap initialization.
            if (objectHeader->template ClassAddr<BaseClass>() == nullptr) {
                return ObjectStatus::ALIVE_OBJECT;
            }
        }
        Region *region = ObjectToRegion(objectHeader);
        auto liveBitmap = region->GetLiveBitmap();
        if (liveBitmap->AtomicTest(objectHeader)) {
            return ObjectStatus::ALIVE_OBJECT;
        }
        if constexpr (!COLLECT_CLASSES) {
            if (ObjectHelpers<LanguageConfig::LANG_TYPE>::IsClassObject(objectHeader)) {
                LOG_DEBUG_OBJECT_EVENTS << "DELETE NON MOVABLE class object " << objectHeader
                                        << " but don't free memory";
                return ObjectStatus::ALIVE_OBJECT;
            }
        }

        if (region->HasFlag(RegionFlag::IS_LARGE_OBJECT)) {
            LOG_DEBUG_OBJECT_EVENTS << "DELETE HUMONGOUS object " << objectHeader;
            // humongous allocator increases size by region size
            *deleteSize_ += region->Size();
            ++(*deleteCount_);
        } else {
            ASSERT(region->HasFlag(RegionFlag::IS_NONMOVABLE));
            LOG_DEBUG_OBJECT_EVENTS << "DELETE NON MOVABLE object " << objectHeader;
        }
        return ObjectStatus::DEAD_OBJECT;
    }

    DEFAULT_COPY_SEMANTIC(NonRegularObjectsDeathChecker);
    DEFAULT_MOVE_SEMANTIC(NonRegularObjectsDeathChecker);

private:
    size_t *deleteSize_;
    size_t *deleteCount_;
};

template <class LanguageConfig>
template <bool ATOMIC, bool CONCURRENTLY>
void G1GC<LanguageConfig>::CollectEmptyRegions(GCTask &task, PandaVector<Region *> *emptyTenuredRegions)
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());
    CollectNonRegularObjects<ATOMIC, CONCURRENTLY>();
    ClearEmptyTenuredMovableRegions<ATOMIC, CONCURRENTLY>(emptyTenuredRegions);
    task.UpdateGCCollectionType(GCCollectionType::TENURED);
}

template <class LanguageConfig>
template <bool ATOMIC, bool CONCURRENTLY>
void G1GC<LanguageConfig>::CollectNonRegularObjects()
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());
    size_t deleteSize = 0;
    size_t deleteCount = 0;
    // Don't collect classes if --g1-track-free-objects is enabled.
    // We need to know size of objects while iterating over all objects in the collected region.
    auto deathChecker =
        g1TrackFreedObjects_
            ? GCObjectVisitor(
                  NonRegularObjectsDeathChecker<LanguageConfig, CONCURRENTLY, false>(&deleteSize, &deleteCount))
            : GCObjectVisitor(
                  NonRegularObjectsDeathChecker<LanguageConfig, CONCURRENTLY, true>(&deleteSize, &deleteCount));
    auto regionVisitor = [this](PandaVector<Region *> &regions) {
        if constexpr (CONCURRENTLY) {
            updateRemsetWorker_->InvalidateRegions(&regions);
        } else {
            updateRemsetWorker_->GCInvalidateRegions(&regions);
        }
    };
    this->GetG1ObjectAllocator()->CollectNonRegularRegions(regionVisitor, deathChecker);
    this->memStats_.template RecordCountFreedTenured<ATOMIC>(deleteCount);
    this->memStats_.template RecordSizeFreedTenured<ATOMIC>(deleteSize);
}

PandaVector<Region *> GetEmptyTenuredRegularRegionsFromQueue(
    PandaPriorityQueue<std::pair<uint32_t, Region *>> garbageRegions)
{
    PandaVector<Region *> emptyTenuredRegions;
    while (!garbageRegions.empty()) {
        auto *topRegion = garbageRegions.top().second;
        if (topRegion->GetLiveBytes() == 0U) {
            emptyTenuredRegions.push_back(topRegion);
        }
        garbageRegions.pop();
    }
    return emptyTenuredRegions;
}

template <class LanguageConfig>
template <bool ATOMIC, bool CONCURRENTLY>
void G1GC<LanguageConfig>::ClearEmptyTenuredMovableRegions(PandaVector<Region *> *emptyTenuredRegions)
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());
    {
        ScopedTiming t1("Region Invalidation", *this->GetTiming());
        if constexpr (CONCURRENTLY) {
            updateRemsetWorker_->InvalidateRegions(emptyTenuredRegions);
        } else {
            updateRemsetWorker_->GCInvalidateRegions(emptyTenuredRegions);
        }
    }
    size_t deleteSize = 0;
    size_t deleteCount = 0;
    auto deathVisitor = [](ObjectHeader *objectHeader) {
        LOG_DEBUG_OBJECT_EVENTS << "DELETE tenured object " << objectHeader;
    };
    for (auto i : *emptyTenuredRegions) {
        deleteCount += i->GetAllocatedObjects();
        deleteSize += i->GetAllocatedBytes();
        ASSERT(i->GetLiveBitmap()->FindFirstMarkedChunks() == nullptr);
        if (g1TrackFreedObjects_) {
            i->IterateOverObjects(deathVisitor);
        }
    }
    {
        ScopedTiming t2("Reset regions", *this->GetTiming());
        if (CONCURRENTLY) {
            this->GetG1ObjectAllocator()
                ->template ResetRegions<RegionFlag::IS_OLD, RegionSpace::ReleaseRegionsPolicy::NoRelease,
                                        OSPagesPolicy::IMMEDIATE_RETURN, true, PandaVector<Region *>>(
                    *emptyTenuredRegions);
        } else {
            this->GetG1ObjectAllocator()
                ->template ResetRegions<RegionFlag::IS_OLD, RegionSpace::ReleaseRegionsPolicy::Release,
                                        OSPagesPolicy::NO_RETURN, false, PandaVector<Region *>>(*emptyTenuredRegions);
        }
    }
    this->memStats_.template RecordCountFreedTenured<ATOMIC>(deleteCount);
    this->memStats_.template RecordSizeFreedTenured<ATOMIC>(deleteSize);
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::NeedToPromote(const Region *region) const
{
    ASSERT(region->HasFlag(RegionFlag::IS_EDEN));
    if (region->HasPinnedObjects()) {
        return true;
    }
    if ((g1PromotionRegionAliveRate_ < PERCENT_100_D) && !this->IsFullGC()) {
        size_t aliveBytes = region->GetLiveBytes();
        double alivePercentage = static_cast<double>(aliveBytes) / region->Size() * PERCENT_100_D;
        if (alivePercentage >= g1PromotionRegionAliveRate_) {
            return true;
        }
    }
    return false;
}

template <class LanguageConfig>
template <bool ATOMIC, RegionFlag REGION_TYPE>
void G1GC<LanguageConfig>::RegionCompactingImpl(Region *region, const ObjectVisitor &movedObjectSaver)
{
    auto objectAllocator = this->GetG1ObjectAllocator();
    // Calculated live bytes in region for all marked objects during MixedMark
    size_t moveSize = region->GetLiveBytes();
    size_t moveCount = 0;
    size_t allocatedSize = region->GetAllocatedBytes();
    ASSERT(moveSize <= allocatedSize);
    size_t deleteSize = allocatedSize - moveSize;
    size_t deleteCount = 0;

    auto moveChecker = [this, &moveCount, &movedObjectSaver](ObjectHeader *src, ObjectHeader *dst) {
        LOG_DEBUG_OBJECT_EVENTS << "MOVE object " << src << " -> " << dst;
        ASSERT(ObjectToRegion(dst)->HasFlag(RegionFlag::IS_OLD));
        this->SetForwardAddress(src, dst);
        ++moveCount;
        movedObjectSaver(dst);
    };

    auto deathChecker = [this, &deleteCount](ObjectHeader *objectHeader) {
        if (IsMarked(objectHeader)) {
            return ObjectStatus::ALIVE_OBJECT;
        }
        ++deleteCount;
        if constexpr (REGION_TYPE == RegionFlag::IS_EDEN) {
            LOG_DEBUG_OBJECT_EVENTS << "DELETE YOUNG object " << objectHeader;
        } else {
            ASSERT(REGION_TYPE == RegionFlag::IS_OLD);
            LOG_DEBUG_OBJECT_EVENTS << "DELETE TENURED object " << objectHeader;
        }
        return ObjectStatus::DEAD_OBJECT;
    };
    if constexpr (REGION_TYPE == RegionFlag::IS_EDEN) {
        if (!this->NeedToPromote(region)) {
            ScopedRegionCollectionInfo collectionInfo(this, "Region compacted", region, true, moveSize);
            if (g1TrackFreedObjects_) {
                // We want to track all freed objects, therefore, iterate over all objects in region.
                objectAllocator->template CompactRegion<RegionFlag::IS_EDEN, false>(region, deathChecker, moveChecker);
            } else {
                objectAllocator->template CompactRegion<RegionFlag::IS_EDEN, true>(region, deathChecker, moveChecker);
                // delete_count is equal to 0 because we don't track allocation in TLABs by a default.
                // We will do it only with PANDA_TRACK_TLAB_ALLOCATIONS key
                ASSERT(deleteCount == 0);
            }
            this->memStats_.template RecordSizeMovedYoung<ATOMIC>(moveSize);
            this->memStats_.template RecordCountMovedYoung<ATOMIC>(moveCount);
            this->memStats_.template RecordSizeFreedYoung<ATOMIC>(deleteSize);
            this->memStats_.template RecordCountFreedYoung<ATOMIC>(deleteCount);
            analytics_.ReportEvacuatedBytes(moveSize);
            analytics_.ReportLiveObjects(moveCount);
        } else {
            RegionPromotionImpl<ATOMIC>(region, movedObjectSaver);
        }
    } else {
        ScopedRegionCollectionInfo collectionInfo(this, "Region compacted", region, false, moveSize);
        ASSERT(region->HasFlag(RegionFlag::IS_OLD));
        ASSERT(!region->HasFlag(RegionFlag::IS_NONMOVABLE) && !region->HasFlag(RegionFlag::IS_LARGE_OBJECT));
        if (g1TrackFreedObjects_) {
            // We want to track all freed objects, therefore, iterate over all objects in region.
            objectAllocator->template CompactRegion<RegionFlag::IS_OLD, false>(region, deathChecker, moveChecker);
        } else {
            objectAllocator->template CompactRegion<RegionFlag::IS_OLD, true>(region, deathChecker, moveChecker);
            size_t allocatedObjects = region->GetAllocatedObjects();
            ASSERT(moveCount <= allocatedObjects);
            ASSERT(deleteCount == 0);
            deleteCount = allocatedObjects - moveCount;
        }
        this->memStats_.template RecordSizeMovedTenured<ATOMIC>(moveSize);
        this->memStats_.template RecordCountMovedTenured<ATOMIC>(moveCount);
        this->memStats_.template RecordSizeFreedTenured<ATOMIC>(deleteSize);
        this->memStats_.template RecordCountFreedTenured<ATOMIC>(deleteCount);
    }
}

template <class LanguageConfig, typename RefUpdater, bool FULL_GC>
void DoUpdateReferencesToMovedObjectsRange(typename GCUpdateRefsWorkersTask<FULL_GC>::MovedObjectsRange *movedObjects,
                                           RefUpdater &refUpdater)
{
    for (auto *obj : *movedObjects) {
        if constexpr (!FULL_GC) {
            obj = obj->IsForwarded() ? GetForwardAddress(obj) : obj;
        }
        ObjectHelpers<LanguageConfig::LANG_TYPE>::template TraverseAllObjectsWithInfo<false>(obj, refUpdater);
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::WorkerTaskProcessing(GCWorkersTask *task, [[maybe_unused]] void *workerData)
{
    switch (task->GetType()) {
        case GCWorkersTaskTypes::TASK_MARKING: {
            auto objectsStack = task->Cast<GCMarkWorkersTask>()->GetMarkingStack();
            MarkStackMixed(objectsStack);
            ASSERT(objectsStack->Empty());
            this->GetInternalAllocator()->Delete(objectsStack);
            break;
        }
        case GCWorkersTaskTypes::TASK_REMARK: {
            auto *objectsStack = task->Cast<GCMarkWorkersTask>()->GetMarkingStack();
            this->MarkStack(&marker_, objectsStack, CalcLiveBytesMarkPreprocess);
            ASSERT(objectsStack->Empty());
            this->GetInternalAllocator()->Delete(objectsStack);
            break;
        }
        case GCWorkersTaskTypes::TASK_FULL_MARK: {
            const ReferenceCheckPredicateT &refEnablePred = []([[maybe_unused]] const ObjectHeader *obj) {
                // process all refs
                return true;
            };
            auto *objectsStack = task->Cast<GCMarkWorkersTask>()->GetMarkingStack();
            this->MarkStack(&marker_, objectsStack, CalcLiveBytesMarkPreprocess, refEnablePred);
            ASSERT(objectsStack->Empty());
            this->GetInternalAllocator()->Delete(objectsStack);
            break;
        }
        case GCWorkersTaskTypes::TASK_REGION_COMPACTING: {
            auto *data = task->Cast<GCRegionCompactWorkersTask>()->GetRegionData();
            Region *region = data->first;
            const ObjectVisitor &movedObjectsSaver = data->second;
            if (region->HasFlag(RegionFlag::IS_EDEN)) {
                RegionCompactingImpl<true, RegionFlag::IS_EDEN>(region, movedObjectsSaver);
            } else if (region->HasFlag(RegionFlag::IS_OLD)) {
                RegionCompactingImpl<true, RegionFlag::IS_OLD>(region, movedObjectsSaver);
            } else {
                LOG(FATAL, GC) << "Unsupported region type";
            }
            this->GetInternalAllocator()->Delete(data);
            break;
        }
        case GCWorkersTaskTypes::TASK_RETURN_FREE_PAGES_TO_OS: {
            PoolManager::GetMmapMemPool()->ReleasePagesInFreePools();
            break;
        }
        case GCWorkersTaskTypes::TASK_ENQUEUE_REMSET_REFS: {
            auto *movedObjectsRange = task->Cast<GCUpdateRefsWorkersTask<false>>()->GetMovedObjectsRange();
            auto *taskUpdatedRefsQueue =
                this->GetInternalAllocator()->template New<GCG1BarrierSet::ThreadLocalCardQueues>();
            EnqueueRemsetRefUpdater<LanguageConfig> refUpdater(this->GetCardTable(), taskUpdatedRefsQueue,
                                                               regionSizeBits_);
            DoUpdateReferencesToMovedObjectsRange<LanguageConfig, decltype(refUpdater), false>(movedObjectsRange,
                                                                                               refUpdater);
            {
                os::memory::LockHolder lock(gcWorkerQueueLock_);
                updatedRefsQueue_->insert(updatedRefsQueue_->end(), taskUpdatedRefsQueue->begin(),
                                          taskUpdatedRefsQueue->end());
            }
            this->GetInternalAllocator()->Delete(movedObjectsRange);
            this->GetInternalAllocator()->Delete(taskUpdatedRefsQueue);
            break;
        }
        default:
            LOG(FATAL, GC) << "Unimplemented for " << GCWorkersTaskTypesToString(task->GetType());
            UNREACHABLE();
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::UpdateCollectionSet(const CollectionSet &collectibleRegions)
{
    collectionSet_ = collectibleRegions;
    for (auto r : collectionSet_) {
        // we don't need to reset flag, because we don't reuse collection_set region
        r->AddFlag(RegionFlag::IS_COLLECTION_SET);
        LOG_DEBUG_GC << "dump region: " << *r;
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::RunPhasesForRegions(panda::GCTask &task, const CollectionSet &collectibleRegions)
{
    if (collectibleRegions.empty()) {
        LOG_DEBUG_GC << "No regions specified for collection " << task.reason;
    }
    ASSERT(concurrentMarkingStack_.Empty());
    this->GetObjectGenAllocator()->InvalidateSpaceData();
    this->GetObjectGenAllocator()->UpdateSpaceData();
    RunGC(task, collectibleRegions);
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::NeedToRunGC(const panda::GCTask &task)
{
    return (task.reason == GCTaskCause::YOUNG_GC_CAUSE) || (task.reason == GCTaskCause::OOM_CAUSE) ||
           (task.reason == GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE) ||
           (task.reason == GCTaskCause::STARTUP_COMPLETE_CAUSE) || (task.reason == GCTaskCause::EXPLICIT_CAUSE) ||
           (task.reason == GCTaskCause::NATIVE_ALLOC_CAUSE) || (task.reason == GCTaskCause::MIXED);
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::NeedFullGC(const panda::GCTask &task)
{
    return this->IsExplicitFull(task) || (task.reason == GCTaskCause::OOM_CAUSE);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::RunPhasesImpl(panda::GCTask &task)
{
    SuspendUpdateRemsetWorkerScope stopUpdateRemsetWorkerScope(updateRemsetWorker_);
    interruptConcurrentFlag_ = false;
    LOG_DEBUG_GC << "G1GC start, reason: " << task.reason;
    LOG_DEBUG_GC << "Footprint before GC: " << this->GetPandaVm()->GetMemStats()->GetFootprintHeap();
    task.UpdateGCCollectionType(GCCollectionType::YOUNG);

    size_t bytesInHeapBeforeMove = this->GetPandaVm()->GetMemStats()->GetFootprintHeap();
    {
        ScopedTiming t("G1 GC", *this->GetTiming());
        {
            GCScopedPauseStats scopedPauseStats(this->GetPandaVm()->GetGCStats());
            this->memStats_.Reset();
            if (NeedToRunGC(task)) {
                // Check there is no concurrent mark running by another thread.
                EnsurePreWrbDisabledInThreads();

                if (NeedFullGC(task)) {
                    task.collectionType = GCCollectionType::FULL;
                    RunFullGC(task);
                } else {
                    bool isMixed = false;
                    if (task.reason == GCTaskCause::MIXED && !interruptConcurrentFlag_) {
                        regionGarbageRateThreshold_ = 0;
                        isMixed = true;
                    } else {
                        // Atomic with acquire order reason: to see changes made by GC thread (which do concurrent
                        // marking and than set is_mixed_gc_required_) in mutator thread which waits for the end of
                        // concurrent marking.
                        isMixed = isMixedGcRequired_.load(std::memory_order_acquire);
                    }
                    task.collectionType = isMixed ? GCCollectionType::MIXED : GCCollectionType::YOUNG;
                    auto collectibleRegions = GetCollectibleRegions(task, isMixed);
                    if (!collectibleRegions.empty() && HaveEnoughSpaceToMove(collectibleRegions)) {
                        // Ordinary collection flow
                        RunMixedGC(task, collectibleRegions);
                    } else {
                        LOG_DEBUG_GC << "Failed to run gc: "
                                     << (collectibleRegions.empty() ? "nothing to collect in movable space"
                                                                    : "not enough free regions to move");
                    }
                }
            }
        }
        if (task.reason == GCTaskCause::MIXED) {
            // There was forced a mixed GC. This GC type sets specific settings.
            // So we need to restore them.
            regionGarbageRateThreshold_ = this->GetSettings()->G1RegionGarbageRateThreshold();
        }
        if (ScheduleMixedGCAndConcurrentMark(task)) {
            RunConcurrentMark(task);
        }
    }
    // Update global and GC memstats based on generational memstats information
    // We will update tenured stats and record allocations, so set 'true' values
    this->UpdateMemStats(bytesInHeapBeforeMove, true, true);

    LOG_DEBUG_GC << "Footprint after GC: " << this->GetPandaVm()->GetMemStats()->GetFootprintHeap();
    this->SetFullGC(false);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::RunFullGC(panda::GCTask &task)
{
    ScopedTiming t("Run Full GC", *this->GetTiming());
    GetG1ObjectAllocator()->template ReleaseEmptyRegions<RegionFlag::IS_OLD, OSPagesPolicy::NO_RETURN>();
    LOG_DEBUG_GC << "Explicit Full GC invocation due to a reason: " << task.reason;
    this->SetFullGC(true);
    FullMarking(task);
    if (!HaveEnoughRegionsToMove(1)) {
        GetG1ObjectAllocator()->ReleaseReservedRegion();
        // After release reserved region we always have minimum 1 region for tenured collection
        ASSERT(HaveEnoughRegionsToMove(1));
    }
    CollectionSet collectionSet = GetFullCollectionSet();
    PrepareYoungRegionsForFullGC(collectionSet);
    auto curRegionIt = collectionSet.Tenured().begin();
    auto endRegionIt = collectionSet.Tenured().end();
    while (curRegionIt != endRegionIt) {
        ASSERT(HaveEnoughRegionsToMove(1));
        CollectionSet cs;
        while ((curRegionIt != endRegionIt) && (HaveEnoughRegionsToMove(cs.Movable().size() + 1))) {
            Region *region = *curRegionIt;
            curRegionIt++;
            if (region->GetGarbageBytes() == 0) {
                double regionFragmentation = region->GetFragmentation();
                if (regionFragmentation < this->GetSettings()->G1FullGCRegionFragmentationRate()) {
                    LOG_DEBUG_GC << "Skip region " << *region << " because it has no garbage inside";
                    continue;
                }
                LOG_DEBUG_GC << "Add region " << *region
                             << " to a collection set because it has a big fragmentation = " << regionFragmentation;
            } else {
                LOG_DEBUG_GC << "Add region " << *region << " to a collection set";
            }
            cs.AddRegion(region);
        }
        UpdateCollectionSet(cs);
        CollectAndMove<true>(cs);
        LOG_DEBUG_GC << "Iterative full GC, collected " << cs.size() << " regions";
    }
    // Reserve a region to prevent OOM in case a lot of garbage in tenured space
    GetG1ObjectAllocator()->ReserveRegionIfNeeded();
    if (!collectionSet.Young().empty()) {
        CollectionSet cs(collectionSet.Young());
        if (HaveEnoughSpaceToMove(cs)) {
            LOG_DEBUG_GC << "Iterative full GC. Collecting " << cs.size() << " young regions";
            UpdateCollectionSet(cs);
            CollectAndMove<true>(cs);
        } else {
            RestoreYoungRegionsAfterFullGC(cs);
            LOG_INFO_GC << "Failed to run gc, not enough free regions for young";
            LOG_INFO_GC << "Accounted total object used bytes = "
                        << PoolManager::GetMmapMemPool()->GetObjectUsedBytes();
        }
    }
    {
        ScopedTiming releasePages("Release Pages in Free Pools", *this->GetTiming());
        bool useGcWorkers = this->GetSettings()->GCWorkersCount() != 0;
        if (useGcWorkers) {
            if (!this->GetWorkersTaskPool()->AddTask(GCWorkersTaskTypes::TASK_RETURN_FREE_PAGES_TO_OS)) {
                PoolManager::GetMmapMemPool()->ReleasePagesInFreePools();
            }
        } else {
            PoolManager::GetMmapMemPool()->ReleasePagesInFreePools();
        }
    }
    this->SetFullGC(false);
    collectionSet_.clear();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::RunMixedGC(panda::GCTask &task, const CollectionSet &collectionSet)
{
    auto startTime = panda::time::GetCurrentTimeInNanos();
    analytics_.ReportCollectionStart(startTime);
    LOG_DEBUG_GC << "Collect regions size:" << collectionSet.size();
    UpdateCollectionSet(collectionSet);
    RunPhasesForRegions(task, collectionSet);
    auto endTime = panda::time::GetCurrentTimeInNanos();
    this->GetStats()->AddTimeValue(endTime - startTime, TimeTypeStats::YOUNG_TOTAL_TIME);
    g1PauseTracker_.AddPauseInNanos(startTime, endTime);
    analytics_.ReportCollectionEnd(endTime, collectionSet);
    collectionSet_.clear();
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::ScheduleMixedGCAndConcurrentMark(panda::GCTask &task)
{
    // Atomic with acquire order reason: to see changes made by GC thread (which do concurrent marking and than set
    // is_mixed_gc_required_) in mutator thread which waits for the end of concurrent marking.
    if (isMixedGcRequired_.load(std::memory_order_acquire)) {
        if (!HaveGarbageRegions()) {
            // Atomic with release order reason: to see changes made by GC thread (which do concurrent marking and
            // than set is_mixed_gc_required_) in mutator thread which waits for the end of concurrent marking.
            isMixedGcRequired_.store(false, std::memory_order_release);
        }
        return false;  // don't run concurrent mark
    }
    concurrentMarkingFlag_ = !interruptConcurrentFlag_ && this->ShouldRunTenuredGC(task);
    // Atomic with relaxed order reason: read variable modified in the same thread
    return concurrentMarkingFlag_.load(std::memory_order_relaxed);
}

template <class LanguageConfig>
template <bool ENABLE_BARRIER>
void G1GC<LanguageConfig>::UpdatePreWrbEntrypointInThreads()
{
    ObjRefProcessFunc entrypointFunc = nullptr;
    if constexpr (ENABLE_BARRIER) {
        auto addr = this->GetBarrierSet()->GetBarrierOperand(panda::mem::BarrierPosition::BARRIER_POSITION_PRE,
                                                             "STORE_IN_BUFF_TO_MARK_FUNC");
        entrypointFunc = std::get<ObjRefProcessFunc>(addr.GetValue());
    }
    auto setEntrypoint = [this, &entrypointFunc](ManagedThread *thread) {
        void *entrypointFuncUntyped = reinterpret_cast<void *>(entrypointFunc);
        ASSERT(thread->GetPreWrbEntrypoint() != entrypointFuncUntyped);
        thread->SetPreWrbEntrypoint(entrypointFuncUntyped);

        // currentPreWrbEntrypoint_ is not required to be set multiple times, but this has to be done under the
        // EnumerateThreads()'s lock, hence the repetition
        currentPreWrbEntrypoint_ = entrypointFunc;
        return true;
    };
    this->GetPandaVm()->GetThreadManager()->EnumerateThreads(setEntrypoint);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::EnsurePreWrbDisabledInThreads()
{
    [[maybe_unused]] auto callback = [](ManagedThread *thread) { return thread->GetPreWrbEntrypoint() == nullptr; };
    ASSERT(this->GetPandaVm()->GetThreadManager()->EnumerateThreads(callback));
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::RunConcurrentMark(panda::GCTask &task)
{
    ASSERT(collectionSet_.empty());
    // Init concurrent marking
    EnablePreWrbInThreads();

    if (this->GetSettings()->BeforeG1ConcurrentHeapVerification()) {
        trace::ScopedTrace postHeapVerifierTrace("PostGCHeapVeriFier before concurrent");
        size_t failCount = this->VerifyHeap();
        if (this->GetSettings()->FailOnHeapVerification() && failCount > 0) {
            LOG(FATAL, GC) << "Heap corrupted after GC, HeapVerifier found " << failCount << " corruptions";
        }
    }
    ConcurrentMarking(task);
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::HaveGarbageRegions()
{
    // Use GetTopGarbageRegions because it doesn't return current regions
    auto regions = GetG1ObjectAllocator()->template GetTopGarbageRegions<false>();
    return HaveGarbageRegions(regions);
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::HaveGarbageRegions(const PandaPriorityQueue<std::pair<uint32_t, Region *>> &regions)
{
    if (regions.empty()) {
        return false;
    }
    auto *topRegion = regions.top().second;
    double garbageRate = static_cast<double>(topRegion->GetGarbageBytes()) / topRegion->Size();
    return garbageRate >= regionGarbageRateThreshold_;
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ProcessDirtyCards()
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());
    updateRemsetWorker_->GCProcessCards();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::CreateUpdateRemsetWorker()
{
    InternalAllocatorPtr allocator = this->GetInternalAllocator();
    // to make TSAN happy because we access updated_refs_queue_ inside constructor of UpdateRemsetWorker
    os::memory::LockHolder lock(queueLock_);
    if (this->GetSettings()->UseThreadPoolForGC()) {
        updateRemsetWorker_ = allocator->template New<UpdateRemsetThread<LanguageConfig>>(
            this, updatedRefsQueue_, &queueLock_, this->GetG1ObjectAllocator()->GetRegionSize(),
            this->GetSettings()->G1EnableConcurrentUpdateRemset(),
            this->GetSettings()->G1MinConcurrentCardsToProcess());
    } else {
        ASSERT(this->GetSettings()->UseTaskManagerForGC());
        updateRemsetWorker_ = allocator->template New<UpdateRemsetTaskQueue<LanguageConfig>>(
            this, updatedRefsQueue_, &queueLock_, this->GetG1ObjectAllocator()->GetRegionSize(),
            this->GetSettings()->G1EnableConcurrentUpdateRemset(),
            this->GetSettings()->G1MinConcurrentCardsToProcess());
    }
    ASSERT(updateRemsetWorker_ != nullptr);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::InitializeImpl()
{
    // GC saved the PandaVM instance, so we get allocator from the PandaVM.
    InternalAllocatorPtr allocator = this->GetInternalAllocator();
    this->CreateCardTable(allocator, PoolManager::GetMmapMemPool()->GetMinObjectAddress(),
                          PoolManager::GetMmapMemPool()->GetTotalObjectSize());

    auto barrierSet =
        allocator->New<GCG1BarrierSet>(allocator, &PreWrbFuncEntrypoint, &PostWrbUpdateCardFuncEntrypoint,
                                       panda::helpers::math::GetIntLog2(this->GetG1ObjectAllocator()->GetRegionSize()),
                                       this->GetCardTable(), updatedRefsQueue_, &queueLock_);
    ASSERT(barrierSet != nullptr);
    this->SetGCBarrierSet(barrierSet);

    this->CreateWorkersTaskPool();
    CreateUpdateRemsetWorker();
    LOG_DEBUG_GC << "G1GC initialized";
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::MarkObject(ObjectHeader *object)
{
    G1GCPauseMarker<LanguageConfig>::Mark(object);
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::MarkObjectIfNotMarked(ObjectHeader *object)
{
    ASSERT(object != nullptr);
    if (this->GetGCPhase() == GCPhase::GC_PHASE_MARK_YOUNG) {
        return mixedMarker_.MarkIfNotMarked(object);
    }
    return marker_.MarkIfNotMarked(object);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::InitGCBitsForAllocationInTLAB([[maybe_unused]] panda::ObjectHeader *object)
{
    LOG(FATAL, GC) << "Not implemented";
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::IsMarked(panda::ObjectHeader const *object) const
{
    return G1GCPauseMarker<LanguageConfig>::IsMarked(object);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::MarkStackMixed(GCMarkingStackType *stack)
{
    ASSERT(stack != nullptr);
    trace::ScopedTrace scopedTrace(__FUNCTION__);
    auto refPred = [this](const ObjectHeader *obj) { return InGCSweepRange(obj); };
    auto visitor = [this, stack, &refPred](const ObjectHeader *object) {
        ASSERT(mixedMarker_.IsMarked(object));
        ValidateObject(nullptr, object);
        auto *objectClass = object->template ClassAddr<BaseClass>();
        // We need annotation here for the FullMemoryBarrier used in InitializeClassByIdEntrypoint
        TSAN_ANNOTATE_HAPPENS_AFTER(objectClass);
        LOG_DEBUG_GC << "Current object: " << GetDebugInfoAboutObject(object);

        ASSERT(!object->IsForwarded());
        ASSERT(InGCSweepRange(object));
        CalcLiveBytesMarkPreprocess(object, objectClass);
        mixedMarker_.MarkInstance(stack, object, objectClass, refPred);
    };
    {
        auto markedObjects = stack->TraverseObjects(visitor);
        os::memory::LockHolder lh(mixedMarkedObjectsMutex_);
        if (mixedMarkedObjects_.empty()) {
            mixedMarkedObjects_ = std::move(markedObjects);
        } else {
            mixedMarkedObjects_.insert(mixedMarkedObjects_.end(), markedObjects.begin(), markedObjects.end());
        }
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::MarkStackFull(GCMarkingStackType *stack)
{
    this->MarkStack(&marker_, stack, CalcLiveBytesMarkPreprocess, GC::EmptyReferenceProcessPredicate);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::MarkReferences(GCMarkingStackType *references, GCPhase gcPhase)
{
    trace::ScopedTrace scopedTrace(__FUNCTION__);
    LOG_DEBUG_GC << "Start marking " << references->Size() << " references";
    // mark refs only on mixed-gc and on full_gc. On concurrent mark we don't handle any references
    if (gcPhase == GCPhase::GC_PHASE_MARK_YOUNG) {
        MarkStackMixed(references);
    } else if (this->IsFullGC()) {
        MarkStackFull(references);
    } else if (gcPhase == GCPhase::GC_PHASE_INITIAL_MARK || gcPhase == GCPhase::GC_PHASE_MARK ||
               gcPhase == GCPhase::GC_PHASE_REMARK) {
        // nothing
    } else {
        LOG_DEBUG_GC << "phase: " << GCScopedPhase::GetPhaseName(gcPhase);
        UNREACHABLE();
    }
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::InGCSweepRange(const ObjectHeader *object) const
{
    ASSERT_DO(!this->collectionSet_.empty() || this->IsFullGC(),
              std::cerr << "Incorrect phase in InGCSweepRange: " << static_cast<size_t>(this->GetGCPhase()) << "\n");
    ASSERT(IsHeapSpace(PoolManager::GetMmapMemPool()->GetSpaceTypeForAddr(object)));
    Region *objRegion = ObjectToRegion(object);
    return objRegion->IsInCollectionSet();
}

static bool RemsetRegionPredicate(const Region *r)
{
    // In case of mixed GC don't process remsets of the tenured regions which are in the collection set
    return !r->HasFlag(IS_COLLECTION_SET);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::RunGC(GCTask &task, const CollectionSet &collectibleRegions)
{
    ASSERT(!this->IsFullGC());
    GCScope<TRACE_TIMING> scopedTrace(__FUNCTION__, this);
    LOG_DEBUG_GC << "GC start";
    uint64_t youngPauseTime;
    {
        time::Timer timer(&youngPauseTime, true);
        HandlePendingDirtyCards();
        MemRange dirtyCardsRange = MixedMarkAndCacheRefs(task, collectibleRegions);
        ClearDirtyAndYoungCards(dirtyCardsRange);
        CollectAndMove<false>(collectibleRegions);
        ReenqueueDirtyCards();
        ClearRefsFromRemsetsCache();
        this->GetObjectGenAllocator()->InvalidateSpaceData();
    }
    if (youngPauseTime > 0) {
        this->GetStats()->AddTimeValue(youngPauseTime, TimeTypeStats::YOUNG_PAUSED_TIME);
    }
    LOG_DEBUG_GC << "G1GC RunGC end";
}

template <class LanguageConfig>
MemRange G1GC<LanguageConfig>::MixedMarkAndCacheRefs(const GCTask &task, const CollectionSet &collectibleRegions)
{
    GCScope<TRACE_TIMING_PHASE> scopedTrace(__FUNCTION__, this, GCPhase::GC_PHASE_MARK_YOUNG);
    bool useGcWorkers = this->GetSettings()->ParallelMarkingEnabled();
    GCMarkingStackType objectsStack(this, useGcWorkers ? this->GetSettings()->GCRootMarkingStackMaxSize() : 0,
                                    useGcWorkers ? this->GetSettings()->GCWorkersMarkingStackMaxSize() : 0,
                                    GCWorkersTaskTypes::TASK_MARKING,
                                    this->GetSettings()->GCMarkingStackNewTasksFrequency());
    for (Region *region : collectibleRegions) {
        region->GetMarkBitmap()->ClearAllBits();
        // Calculate live bytes during marking phase
        region->SetLiveBytes(0U);
    }
    ASSERT(this->GetReferenceProcessor()->GetReferenceQueueSize() ==
           0);  // all references should be processed on previous-gc
    // Iterate over roots and add other roots
    // 0. Pre-process refs queue and fill RemSets (should be done later in background)
    // Note: We need to process only tenured -> young refs,
    // since we reach this by graph from tenured roots,
    // because we will process all young regions at young GC we will find all required references
    RefCacheBuilder<LanguageConfig> builder(this, &uniqueRefsFromRemsets_, regionSizeBits_, &objectsStack);
    auto refsChecker = [this, &builder](const MemRange &memRange, Region *region) {
        IterateOverRefsInMemRange(memRange, region, builder);
        return builder.AllCrossRegionRefsProcessed();
    };
    MemRange dirtyCardsRange = CacheRefsFromRemsets(refsChecker);

    auto refPred = [this](const ObjectHeader *obj) { return this->InGCSweepRange(obj); };
    GCRootVisitor gcMarkCollectionSet = [&objectsStack, this, &refPred](const GCRoot &gcRoot) {
        ObjectHeader *rootObject = gcRoot.GetObjectHeader();
        ObjectHeader *fromObject = gcRoot.GetFromObjectHeader();
        LOG_DEBUG_GC << "Handle root " << GetDebugInfoAboutObject(rootObject) << " from: " << gcRoot.GetType();
        if (UNLIKELY(fromObject != nullptr) &&
            this->IsReference(fromObject->NotAtomicClassAddr<BaseClass>(), fromObject, refPred)) {
            LOG_DEBUG_GC << "Add reference: " << GetDebugInfoAboutObject(fromObject) << " to stack";
            mixedMarker_.Mark(fromObject);
            this->ProcessReference(&objectsStack, fromObject->NotAtomicClassAddr<BaseClass>(), fromObject,
                                   GC::EmptyReferenceProcessPredicate);
        } else {
            // Skip non-collection-set roots
            auto rootObjectPtr = gcRoot.GetObjectHeader();
            ASSERT(rootObjectPtr != nullptr);
            if (mixedMarker_.MarkIfNotMarked(rootObjectPtr)) {
                ASSERT(this->InGCSweepRange(rootObjectPtr));
                LOG_DEBUG_GC << "root " << GetDebugInfoAboutObject(rootObjectPtr);
                objectsStack.PushToStack(gcRoot.GetType(), rootObjectPtr);
            } else {
                LOG_DEBUG_GC << "Skip root for young mark: " << std::hex << rootObjectPtr;
            }
        }
    };

    analytics_.ReportMarkingStart(panda::time::GetCurrentTimeInNanos());
    {
        GCScope<TRACE_TIMING> markingCollectionSetRootsTrace("Marking roots collection-set", this);

        this->VisitRoots(gcMarkCollectionSet, VisitGCRootFlags::ACCESS_ROOT_NONE);
    }
    {
        GCScope<TRACE_TIMING> markStackTiming("MarkStack", this);
        this->MarkStackMixed(&objectsStack);
        ASSERT(objectsStack.Empty());
        if (useGcWorkers) {
            this->GetWorkersTaskPool()->WaitUntilTasksEnd();
        }
    }

    auto refClearPred = [this](const ObjectHeader *obj) { return this->InGCSweepRange(obj); };
    this->GetPandaVm()->HandleReferences(task, refClearPred);

    analytics_.ReportMarkingEnd(panda::time::GetCurrentTimeInNanos());

    // HandleReferences could write a new barriers - so we need to handle them before moving
    ProcessDirtyCards();
    return dirtyCardsRange;
}

template <class LanguageConfig>
HeapVerifierIntoGC<LanguageConfig> G1GC<LanguageConfig>::CollectVerificationInfo(const CollectionSet &collectionSet)
{
    HeapVerifierIntoGC<LanguageConfig> collectVerifier(this->GetPandaVm()->GetHeapManager());
    if (this->GetSettings()->IntoGCHeapVerification()) {
        ScopedTiming collectVerificationTiming(__FUNCTION__, *this->GetTiming());
        PandaVector<MemRange> memRanges;
        memRanges.reserve(collectionSet.size());
        std::for_each(collectionSet.begin(), collectionSet.end(),
                      [&memRanges](const Region *region) { memRanges.emplace_back(region->Begin(), region->End()); });
        collectVerifier.CollectVerificationInfo(std::move(memRanges));
    }
    return collectVerifier;
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::VerifyCollectAndMove(HeapVerifierIntoGC<LanguageConfig> &&collectVerifier,
                                                const CollectionSet &collectionSet)
{
    if (this->GetSettings()->IntoGCHeapVerification()) {
        ScopedTiming verificationTiming(__FUNCTION__, *this->GetTiming());
        PandaVector<MemRange> aliveMemRange;
        std::for_each(collectionSet.begin(), collectionSet.end(), [&aliveMemRange](const Region *region) {
            if (region->HasFlag(RegionFlag::IS_PROMOTED)) {
                aliveMemRange.emplace_back(region->Begin(), region->End());
            }
        });
        size_t failsCount = collectVerifier.VerifyAll(std::move(aliveMemRange));
        if (this->GetSettings()->FailOnHeapVerification() && failsCount > 0U) {
            PandaStringStream logStream;
            logStream << "Collection set size: " << collectionSet.size() << "\n";
            for (const auto r : collectionSet) {
                logStream << *r << (r->HasFlag(RegionFlag::IS_PROMOTED) ? " was promoted\n" : "\n");
            }
            LOG(FATAL, GC) << "Heap was corrupted during CollectAndMove GC phase, HeapVerifier found " << failsCount
                           << " corruptions\n"
                           << logStream.str();
        }
    }
}

template <class LanguageConfig>
template <bool FULL_GC>
// NOLINTNEXTLINE(readability-function-size)
bool G1GC<LanguageConfig>::CollectAndMove(const CollectionSet &collectionSet)
{
    GCScope<TRACE_TIMING_PHASE> scope(__FUNCTION__, this, GCPhase::GC_PHASE_COLLECT_YOUNG_AND_MOVE);
    LOG_DEBUG_GC << "== G1GC CollectAndMove start ==";
    auto internalAllocator = this->GetInternalAllocator();
    bool useGcWorkers = this->GetSettings()->ParallelCompactingEnabled();

    PandaVector<PandaVector<ObjectHeader *> *> movedObjectsVector;
    HeapVerifierIntoGC<LanguageConfig> collectVerifier = this->CollectVerificationInfo(collectionSet);
    {
        GCScope<TRACE_TIMING> compactRegions("CompactRegions", this);
        analytics_.ReportEvacuationStart(panda::time::GetCurrentTimeInNanos());
        if constexpr (FULL_GC) {
            if (!useGcWorkers) {
                auto vector = internalAllocator->template New<PandaVector<ObjectHeader *>>();
                movedObjectsVector.push_back(vector);
            }
        }
        for (auto r : collectionSet.Young()) {
            this->DoRegionCompacting<RegionFlag::IS_EDEN, FULL_GC>(r, useGcWorkers, &movedObjectsVector);
        }
        for (auto r : collectionSet.Tenured()) {
            this->DoRegionCompacting<RegionFlag::IS_OLD, FULL_GC>(r, useGcWorkers, &movedObjectsVector);
        }

        if (useGcWorkers) {
            this->GetWorkersTaskPool()->WaitUntilTasksEnd();
        }

        analytics_.ReportEvacuationEnd(panda::time::GetCurrentTimeInNanos());
    }

    MovedObjectsContainer<FULL_GC> *movedObjectsContainer = nullptr;
    if constexpr (FULL_GC) {
        movedObjectsContainer = &movedObjectsVector;
    } else {
        movedObjectsContainer = &mixedMarkedObjects_;
    }

    {
        os::memory::LockHolder lock(queueLock_);
        analytics_.ReportUpdateRefsStart(panda::time::GetCurrentTimeInNanos());
        if (this->GetSettings()->ParallelRefUpdatingEnabled()) {
            UpdateRefsToMovedObjects<FULL_GC, true>(movedObjectsContainer);
        } else {
            UpdateRefsToMovedObjects<FULL_GC, false>(movedObjectsContainer);
        }
        analytics_.ReportUpdateRefsEnd(panda::time::GetCurrentTimeInNanos());
        ActualizeRemSets();
    }

    VerifyCollectAndMove(std::move(collectVerifier), collectionSet);
    SweepRegularVmRefs();

    auto objectAllocator = this->GetG1ObjectAllocator();
    if (!collectionSet.Young().empty()) {
        objectAllocator->ResetYoungAllocator();
    }
    {
        GCScope<TRACE_TIMING> resetRegions("ResetRegions", this);
        if (!this->IsFullGC()) {
            objectAllocator->template ResetRegions<RegionFlag::IS_OLD, RegionSpace::ReleaseRegionsPolicy::NoRelease,
                                                   OSPagesPolicy::IMMEDIATE_RETURN, false>(collectionSet.Tenured());
        } else {
            objectAllocator->template ResetRegions<RegionFlag::IS_OLD, RegionSpace::ReleaseRegionsPolicy::Release,
                                                   OSPagesPolicy::NO_RETURN, false>(collectionSet.Tenured());
        }
    }
    {
        // Don't forget to delete all temporary elements
        GCScope<TRACE_TIMING> clearMovedObjects("ClearMovedObjects", this);
        if constexpr (FULL_GC) {
            if (useGcWorkers) {
                for (auto r : movedObjectsVector) {
                    internalAllocator->Delete(r);
                }
            } else {
                ASSERT(movedObjectsVector.size() == 1);
                internalAllocator->Delete(movedObjectsVector.back());
            }
        } else {
            for (auto r : mixedMarkedObjects_) {
                internalAllocator->Delete(r);
            }
            mixedMarkedObjects_.clear();
        }
    }

    LOG_DEBUG_GC << "== G1GC CollectAndMove end ==";
    return true;
}

template <class LanguageConfig>
template <bool FULL_GC, bool NEED_LOCK>
std::conditional_t<FULL_GC, UpdateRemsetRefUpdater<LanguageConfig, NEED_LOCK>, EnqueueRemsetRefUpdater<LanguageConfig>>
G1GC<LanguageConfig>::CreateRefUpdater([[maybe_unused]] GCG1BarrierSet::ThreadLocalCardQueues *updatedRefQueue) const
{
    if constexpr (FULL_GC) {
        return UpdateRemsetRefUpdater<LanguageConfig, NEED_LOCK>(regionSizeBits_);
    } else {
        return EnqueueRemsetRefUpdater<LanguageConfig>(this->GetCardTable(), updatedRefQueue, regionSizeBits_);
    }
}

template <class LanguageConfig>
template <bool FULL_GC, bool USE_WORKERS>
void G1GC<LanguageConfig>::UpdateRefsToMovedObjects(MovedObjectsContainer<FULL_GC> *movedObjectsContainer)
{
    GCScope<TRACE_TIMING> scope(__FUNCTION__, this);
    // Currently lock for RemSet too much influences for pause, so don't use workers on FULL-GC
    constexpr bool ENABLE_WORKERS = USE_WORKERS && !FULL_GC;
    auto internalAllocator = this->GetInternalAllocator();
    auto *updatedRefQueue =
        (ENABLE_WORKERS) ? internalAllocator->template New<GCG1BarrierSet::ThreadLocalCardQueues>() : updatedRefsQueue_;
    // NEED_LOCK is true <=> when ENABLE_WORKERS is true
    auto refUpdater = this->CreateRefUpdater<FULL_GC, ENABLE_WORKERS>(updatedRefQueue);
    //  update reference from objects which were moved while garbage collection
    LOG_DEBUG_GC << "=== Update ex-cset -> ex-cset references. START. ===";
    {
        ScopedTiming t("UpdateMovedObjectsReferences", *this->GetTiming());
        for (auto *movedObjects : *movedObjectsContainer) {
            if constexpr (ENABLE_WORKERS) {
                auto rangeBegin = movedObjects->begin();
                auto rangeEnd = rangeBegin;
                while (rangeBegin != movedObjects->end()) {
                    if (std::distance(rangeBegin, movedObjects->end()) < GCUpdateRefsWorkersTask<false>::RANGE_SIZE) {
                        rangeEnd = movedObjects->end();
                    } else {
                        std::advance(rangeEnd, GCUpdateRefsWorkersTask<false>::RANGE_SIZE);
                    }
                    auto *movedObjectsRange =
                        internalAllocator->template New<typename GCUpdateRefsWorkersTask<false>::MovedObjectsRange>(
                            rangeBegin, rangeEnd);
                    rangeBegin = rangeEnd;
                    GCUpdateRefsWorkersTask<false> gcWorkerTask(movedObjectsRange);
                    if (this->GetWorkersTaskPool()->AddTask(GCUpdateRefsWorkersTask<false>(gcWorkerTask))) {
                        continue;
                    }
                    // Couldn't add new task, so do task processing immediately
                    this->WorkerTaskProcessing(&gcWorkerTask, nullptr);
                }
            } else {  // GC workers are not used
                typename GCUpdateRefsWorkersTask<FULL_GC>::MovedObjectsRange movedObjectsRange(movedObjects->begin(),
                                                                                               movedObjects->end());
                DoUpdateReferencesToMovedObjectsRange<LanguageConfig, decltype(refUpdater), FULL_GC>(&movedObjectsRange,
                                                                                                     refUpdater);
            }
        }
    }
    LOG_DEBUG_GC << "=== Update ex-cset -> ex-cset references. END. ===";

    // update references from objects which are not part of collection set
    LOG_DEBUG_GC << "=== Update non ex-cset -> ex-cset references. START. ===";
    if constexpr (FULL_GC) {
        UpdateRefsFromRemSets(refUpdater);
    } else {
        VisitRemSets(refUpdater);
    }
    LOG_DEBUG_GC << "=== Update non ex-cset -> ex-cset references. END. ===";
    if constexpr (ENABLE_WORKERS) {
        {
            os::memory::LockHolder lock(gcWorkerQueueLock_);
            updatedRefsQueue_->insert(updatedRefsQueue_->end(), updatedRefQueue->begin(), updatedRefQueue->end());
            this->GetInternalAllocator()->Delete(updatedRefQueue);
        }
        this->GetWorkersTaskPool()->WaitUntilTasksEnd();
    }
    this->CommonUpdateRefsToMovedObjects();
}

template <class LanguageConfig>
NO_THREAD_SAFETY_ANALYSIS void G1GC<LanguageConfig>::OnPauseMark(GCTask &task, GCMarkingStackType *objectsStack,
                                                                 bool useGcWorkers)
{
    GCScope<TRACE_TIMING> scope(__FUNCTION__, this);
    LOG_DEBUG_GC << "OnPause marking started";
    auto *objectAllocator = GetG1ObjectAllocator();
    this->MarkImpl(
        &marker_, objectsStack, CardTableVisitFlag::VISIT_DISABLED,
        // process references on FULL-GC
        GC::EmptyReferenceProcessPredicate,
        // non-young mem-range checker
        [objectAllocator](MemRange &memRange) { return !objectAllocator->IsIntersectedWithYoung(memRange); },
        // mark predicate
        CalcLiveBytesMarkPreprocess);
    if (useGcWorkers) {
        this->GetWorkersTaskPool()->WaitUntilTasksEnd();
    }
    /**
     * We don't collect non-movable regions right now, if there was a reference from non-movable to
     * young/tenured region then we reset markbitmap for non-nonmovable, but don't update livebitmap and we
     * can traverse over non-reachable object (in CacheRefsFromRemsets) and visit DEAD object in
     * tenured space (was delete on young-collection or in Iterative-full-gc phase.
     */
    auto refClearPred = []([[maybe_unused]] const ObjectHeader *obj) { return true; };
    this->GetPandaVm()->HandleReferences(task, refClearPred);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::FullMarking(panda::GCTask &task)
{
    GCScope<TRACE_TIMING_PHASE> scope(__FUNCTION__, this, GCPhase::GC_PHASE_MARK);
    auto *objectAllocator = GetG1ObjectAllocator();
    bool useGcWorkers = this->GetSettings()->ParallelMarkingEnabled();

    GCMarkingStackType fullCollectionStack(this, useGcWorkers ? this->GetSettings()->GCRootMarkingStackMaxSize() : 0,
                                           useGcWorkers ? this->GetSettings()->GCWorkersMarkingStackMaxSize() : 0,
                                           GCWorkersTaskTypes::TASK_FULL_MARK,
                                           this->GetSettings()->GCMarkingStackNewTasksFrequency());

    InitialMark(fullCollectionStack);

    this->OnPauseMark(task, &fullCollectionStack, useGcWorkers);
    // We will sweep VM refs in tenured space during mixed collection, but only for non empty regions.
    // therefore, sweep it here only for NonMovable, Humongous objects, and empty movable regions:
    SweepNonRegularVmRefs();
    auto allRegions = objectAllocator->GetAllRegions();
    for (auto *r : allRegions) {
        if (r->GetLiveBitmap() != nullptr) {
            r->CloneMarkBitmapToLiveBitmap();
        }
    }
    // Force card updater here, after swapping bitmap, to skip dead objects
    ProcessDirtyCards();
    auto garbageRegions = GetG1ObjectAllocator()->template GetTopGarbageRegions<false>();
    auto emptyTenuredRegions = GetEmptyTenuredRegularRegionsFromQueue(std::move(garbageRegions));
    CollectEmptyRegions<false, false>(task, &emptyTenuredRegions);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ConcurrentMarking(panda::GCTask &task)
{
    {
        PauseTimeGoalDelay();
        auto scopedTracker = g1PauseTracker_.CreateScope();
        GCScopedPauseStats scopedPauseStats(this->GetPandaVm()->GetGCStats(), nullptr, PauseTypeStats::COMMON_PAUSE);
        InitialMark(concurrentMarkingStack_);
    }

    LOG_DEBUG_GC << "Concurrent marking started";
    ConcurrentMark(&concurrentMarkingStack_);
    PauseTimeGoalDelay();
    // weak refs shouldn't be added to the queue on concurrent-mark
    ASSERT(this->GetReferenceProcessor()->GetReferenceQueueSize() == 0);

    DisablePreWrbInThreads();

    concurrentMarkingFlag_ = false;
    if (!interruptConcurrentFlag_) {
        Remark(task);
        // Enable mixed GC
        auto garbageRegions = GetG1ObjectAllocator()->template GetTopGarbageRegions<false>();
        if (HaveGarbageRegions(garbageRegions)) {
            // Atomic with release order reason: to see changes made by GC thread (which do concurrent marking
            // and than set is_mixed_gc_required_) in mutator thread which waits for the end of concurrent
            // marking.
            isMixedGcRequired_.store(true, std::memory_order_release);
        }

        {
            ScopedTiming t("Concurrent Sweep", *this->GetTiming());
            ConcurrentScope concurrentScope(this);
            auto emptyTenuredRegions = GetEmptyTenuredRegularRegionsFromQueue(std::move(garbageRegions));
            if (this->IsConcurrencyAllowed()) {
                CollectEmptyRegions<true, true>(task, &emptyTenuredRegions);
            } else {
                CollectEmptyRegions<false, false>(task, &emptyTenuredRegions);
            }
        }
    } else {
        concurrentMarkingStack_.Clear();
        ClearSatb();
    }
    ASSERT(concurrentMarkingStack_.Empty());
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::PauseTimeGoalDelay()
{
    if (this->GetSettings()->G1EnablePauseTimeGoal() && !interruptConcurrentFlag_) {
        auto start = panda::time::GetCurrentTimeInMicros();
        // Instead of max pause it should be estimated to calculate delay
        auto remained = g1PauseTracker_.MinDelayBeforeMaxPauseInMicros(panda::time::GetCurrentTimeInMicros());
        if (remained > 0) {
            ConcurrentScope concurrentScope(this);
            os::memory::LockHolder lh(concurrentMarkMutex_);
            while (!interruptConcurrentFlag_ && remained > 0) {
                auto ms = remained / panda::os::time::MILLIS_TO_MICRO;
                auto ns = (remained - ms * panda::os::time::MILLIS_TO_MICRO) * panda::os::time::MICRO_TO_NANO;
                concurrentMarkCondVar_.TimedWait(&concurrentMarkMutex_, ms, ns);
                remained -= panda::time::GetCurrentTimeInMicros() - start;
            }
        }
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::InitialMark(GCMarkingStackType &markingStack)
{
    {
        // First we need to unmark all heap
        GCScope<TRACE_TIMING> unMarkScope("UnMark", this);
        LOG_DEBUG_GC << "Start unmark all heap before mark";
        auto allRegion = GetG1ObjectAllocator()->GetAllRegions();
        for (Region *r : allRegion) {
            auto *bitmap = r->GetMarkBitmap();
            // Calculate live bytes during mark-phase
            r->SetLiveBytes(0U);
            // unmark full-heap except Humongous-space
            bitmap->ClearAllBits();
        }
#ifndef NDEBUG
        this->GetObjectAllocator()->IterateOverObjects(
            [this](ObjectHeader *obj) { ASSERT(!this->marker_.IsMarked(obj)); });
#endif
    }
    ASSERT(this->GetReferenceProcessor()->GetReferenceQueueSize() ==
           0);  // all references should be processed on mixed-gc
    {
        GCScope<TRACE_TIMING_PHASE> initialMarkScope("InitialMark", this, GCPhase::GC_PHASE_INITIAL_MARK);
        // Collect non-heap roots.
        // Mark the whole heap by using only these roots.
        // The interregion roots will be processed at pause

        // InitialMark. STW
        GCRootVisitor gcMarkRoots = [this, &markingStack](const GCRoot &gcRoot) {
            ValidateObject(gcRoot.GetType(), gcRoot.GetObjectHeader());
            if (marker_.MarkIfNotMarked(gcRoot.GetObjectHeader())) {
                markingStack.PushToStack(gcRoot.GetType(), gcRoot.GetObjectHeader());
            }
        };
        this->VisitRoots(gcMarkRoots, VisitGCRootFlags::ACCESS_ROOT_ALL);
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ConcurrentMark(GCMarkingStackType *objectsStack)
{
    ConcurrentScope concurrentScope(this);
    GCScope<TRACE_TIMING_PHASE> scope(__FUNCTION__, this, GCPhase::GC_PHASE_MARK);
    this->ConcurentMarkImpl(objectsStack);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::Remark(panda::GCTask const &task)
{
    /**
     * Make remark on pause to have all marked objects in tenured space, it gives possibility to check objects in
     * remsets. If they are not marked - we don't process this object, because it's dead already
     */
    auto scopedTracker = g1PauseTracker_.CreateScope();
    GCScope<TIMING_PHASE> gcScope(__FUNCTION__, this, GCPhase::GC_PHASE_REMARK);
    GCScopedPauseStats scopedPauseStats(this->GetPandaVm()->GetGCStats(), nullptr, PauseTypeStats::REMARK_PAUSE);
    {
        ScopedTiming t("Stack Remarking", *this->GetTiming());
        bool useGcWorkers = this->GetSettings()->ParallelMarkingEnabled();
        GCMarkingStackType stack(this, useGcWorkers ? this->GetSettings()->GCRootMarkingStackMaxSize() : 0,
                                 useGcWorkers ? this->GetSettings()->GCWorkersMarkingStackMaxSize() : 0,
                                 GCWorkersTaskTypes::TASK_REMARK,
                                 this->GetSettings()->GCMarkingStackNewTasksFrequency());

        // The mutator may create new regions.
        // If so we should bind bitmaps of new regions.
        DrainSatb(&stack);
        this->MarkStack(&marker_, &stack, CalcLiveBytesMarkPreprocess);

        if (useGcWorkers) {
            this->GetWorkersTaskPool()->WaitUntilTasksEnd();
        }

        // ConcurrentMark doesn't visit young objects - so we can't clear references which are in young-space because we
        // don't know which objects are marked. We will process them on young/mixed GC separately later, here we process
        // only refs in tenured-space
        auto refClearPred = []([[maybe_unused]] const ObjectHeader *obj) {
            return !ObjectToRegion(obj)->HasFlag(RegionFlag::IS_EDEN);
        };
        this->GetPandaVm()->HandleReferences(task, refClearPred);
    }

    // We will sweep VM refs in tenured space during mixed collection,
    // therefore, sweep it here only for NonMovable and Humongous objects:
    SweepNonRegularVmRefs();
    auto g1Allocator = this->GetG1ObjectAllocator();
    auto allRegions = g1Allocator->GetAllRegions();
    for (const auto &region : allRegions) {
        if (region->HasFlag(IS_OLD) || region->HasFlag(IS_NONMOVABLE)) {
            region->SwapMarkBitmap();
        }
    }
    // Force card updater here, after swapping bitmap, to skip dead objects
    ProcessDirtyCards();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::SweepNonRegularVmRefs()
{
    ScopedTiming scopedTiming(__FUNCTION__, *this->GetTiming());

    this->GetPandaVm()->SweepVmRefs([this](ObjectHeader *object) {
        Region *region = ObjectToRegion(object);
        if (region->HasFlag(RegionFlag::IS_EDEN)) {
            return ObjectStatus::ALIVE_OBJECT;
        }
        bool nonRegularObject =
            region->HasFlag(RegionFlag::IS_NONMOVABLE) || region->HasFlag(RegionFlag::IS_LARGE_OBJECT);
        if (!nonRegularObject) {
            ASSERT(region->GetLiveBytes() != 0U || !this->IsMarked(object));
            return region->GetLiveBytes() == 0U ? ObjectStatus::DEAD_OBJECT : ObjectStatus::ALIVE_OBJECT;
        }
        return this->IsMarked(object) ? ObjectStatus::ALIVE_OBJECT : ObjectStatus::DEAD_OBJECT;
    });
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::SweepRegularVmRefs()
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());

    this->GetPandaVm()->SweepVmRefs([this](ObjectHeader *obj) {
        if (this->InGCSweepRange(obj)) {
            return ObjectStatus::DEAD_OBJECT;
        }
        return ObjectStatus::ALIVE_OBJECT;
    });
}

template <class LanguageConfig>
CollectionSet G1GC<LanguageConfig>::GetCollectibleRegions(panda::GCTask const &task, bool isMixed)
{
    ASSERT(!this->IsFullGC());
    ScopedTiming scopedTiming(__FUNCTION__, *this->GetTiming());
    auto g1Allocator = this->GetG1ObjectAllocator();
    LOG_DEBUG_GC << "Start GetCollectibleRegions is_mixed: " << isMixed << " reason: " << task.reason;
    CollectionSet collectionSet(g1Allocator->GetYoungRegions());
    if (isMixed) {
        if (!this->GetSettings()->G1EnablePauseTimeGoal()) {
            AddOldRegionsMaxAllowed(collectionSet);
        } else {
            AddOldRegionsAccordingPauseTimeGoal(collectionSet);
        }
    }
    LOG_DEBUG_GC << "collectible_regions size: " << collectionSet.size() << " young " << collectionSet.Young().size()
                 << " old " << std::distance(collectionSet.Young().end(), collectionSet.end())
                 << " reason: " << task.reason << " is_mixed: " << isMixed;
    return collectionSet;
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::AddOldRegionsMaxAllowed(CollectionSet &collectionSet)
{
    auto regions = this->GetG1ObjectAllocator()->template GetTopGarbageRegions<false>();
    for (size_t i = 0; i < numberOfMixedTenuredRegions_ && !regions.empty(); i++) {
        auto *garbageRegion = regions.top().second;
        regions.pop();
        ASSERT(!garbageRegion->HasFlag(IS_EDEN));
        ASSERT(!garbageRegion->HasPinnedObjects());
        ASSERT(!garbageRegion->HasFlag(IS_RESERVED));
        ASSERT(garbageRegion->GetAllocatedBytes() != 0U);
        double garbageRate = static_cast<double>(garbageRegion->GetGarbageBytes()) / garbageRegion->GetAllocatedBytes();
        if (garbageRate >= regionGarbageRateThreshold_) {
            LOG_DEBUG_GC << "Garbage percentage in " << std::hex << garbageRegion << " region = " << std::dec
                         << garbageRate << " %, add to collection set";
            collectionSet.AddRegion(garbageRegion);
        } else {
            LOG_DEBUG_GC << "Garbage percentage in " << std::hex << garbageRegion << " region = " << std::dec
                         << garbageRate << " %, don't add to collection set";
            break;
        }
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::AddOldRegionsAccordingPauseTimeGoal(CollectionSet &collectionSet)
{
    auto gcPauseTimeBudget =
        static_cast<int64_t>(this->GetSettings()->GetG1MaxGcPauseInMillis() * panda::os::time::MILLIS_TO_MICRO);
    auto regions = this->GetG1ObjectAllocator()->template GetTopGarbageRegions<false>();
    // add at least one old region to guarantee a progress in mixed collection
    auto *topRegion = regions.top().second;
    collectionSet.AddRegion(topRegion);
    auto expectedTopRegionCollectionTime = analytics_.PredictOldCollectionTimeInMicros(topRegion);
    if (gcPauseTimeBudget < expectedTopRegionCollectionTime) {
        LOG_DEBUG_GC << "Not enough budget to add more than one old region";
        return;
    }
    gcPauseTimeBudget -= expectedTopRegionCollectionTime;
    auto predictionError = analytics_.EstimatePredictionErrorInMicros();
    if (gcPauseTimeBudget < predictionError) {
        LOG_DEBUG_GC << "Not enough budget to add old regions";
        return;
    }
    gcPauseTimeBudget -= predictionError;
    auto expectedYoungCollectionTime = analytics_.PredictYoungCollectionTimeInMicros(collectionSet.Young().size());
    if (gcPauseTimeBudget < expectedYoungCollectionTime) {
        LOG_DEBUG_GC << "Not enough budget to add old regions";
        return;
    }
    gcPauseTimeBudget -= expectedYoungCollectionTime;

    regions.pop();
    while (!regions.empty()) {
        auto &scoreAndRegion = regions.top();
        auto *garbageRegion = scoreAndRegion.second;
        ASSERT(!garbageRegion->HasFlag(IS_EDEN));
        ASSERT(!garbageRegion->HasPinnedObjects());
        ASSERT(!garbageRegion->HasFlag(IS_RESERVED));
        ASSERT(garbageRegion->GetAllocatedBytes() != 0U);

        regions.pop();

        double garbageRate = static_cast<double>(garbageRegion->GetGarbageBytes()) / garbageRegion->GetAllocatedBytes();
        if (garbageRate < regionGarbageRateThreshold_) {
            LOG_DEBUG_GC << "Garbage percentage in " << std::hex << garbageRegion << " region = " << std::dec
                         << garbageRate << " %, don't add to collection set";
            break;
        }

        auto expectedRegionCollectionTime = analytics_.PredictOldCollectionTimeInMicros(garbageRegion);
        if (gcPauseTimeBudget < expectedRegionCollectionTime) {
            LOG_DEBUG_GC << "Not enough budget to add old regions anymore";
            break;
        }

        gcPauseTimeBudget -= expectedRegionCollectionTime;

        LOG_DEBUG_GC << "Garbage percentage in " << std::hex << garbageRegion << " region = " << std::dec << garbageRate
                     << " %, add to collection set";
        collectionSet.AddRegion(garbageRegion);
    }
}

template <class LanguageConfig>
CollectionSet G1GC<LanguageConfig>::GetFullCollectionSet()
{
    ASSERT(this->IsFullGC());
    // FillRemSet should be always finished before GetCollectibleRegions
    ASSERT(updateRemsetWorker_->GetQueueSize() == 0);
    auto g1Allocator = this->GetG1ObjectAllocator();
    g1Allocator->ClearCurrentTenuredRegion();
    CollectionSet collectionSet(g1Allocator->GetYoungRegions());
    auto movableGarbageRegions = g1Allocator->template GetTopGarbageRegions<true>();
    LOG_DEBUG_GC << "Regions for FullGC:";
    while (!movableGarbageRegions.empty()) {
        auto *region = movableGarbageRegions.top().second;
        movableGarbageRegions.pop();
        if (region->HasFlag(IS_EDEN) || region->HasPinnedObjects()) {
            LOG_DEBUG_GC << (region->HasFlags(IS_EDEN) ? "Young regions" : "Region with pinned objects") << " ("
                         << *region << ") is not added to collection set";
            continue;
        }
        LOG_DEBUG_GC << *region;
        ASSERT(!region->HasFlag(IS_NONMOVABLE) && !region->HasFlag(IS_LARGE_OBJECT));
        ASSERT(region->HasFlag(IS_OLD));
        collectionSet.AddRegion(region);
    }
    return collectionSet;
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::HaveEnoughSpaceToMove(const CollectionSet &collectibleRegions)
{
    return HaveEnoughRegionsToMove(collectibleRegions.Movable().size());
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::HaveEnoughRegionsToMove(size_t num)
{
    return GetG1ObjectAllocator()->HaveTenuredSize(num) && GetG1ObjectAllocator()->HaveFreeRegions(num);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::OnThreadTerminate(ManagedThread *thread, mem::BuffersKeepingFlag keepBuffers)
{
    InternalAllocatorPtr allocator = this->GetInternalAllocator();
    // The method must be called while the lock which guards thread/coroutine list is hold
    LOG(DEBUG, GC) << "Call OnThreadTerminate";
    PandaVector<ObjectHeader *> *preBuff = nullptr;
    if (keepBuffers == mem::BuffersKeepingFlag::KEEP) {
        preBuff = allocator->New<PandaVector<ObjectHeader *>>(*thread->GetPreBuff());
        thread->GetPreBuff()->clear();
    } else {  // keep_buffers == mem::BuffersKeepingFlag::DELETE
        preBuff = thread->MovePreBuff();
    }
    ASSERT(preBuff != nullptr);
    {
        os::memory::LockHolder lock(satbAndNewobjBufLock_);
        satbBuffList_.push_back(preBuff);
    }
    {
        auto *localBuffer = thread->GetG1PostBarrierBuffer();
        ASSERT(localBuffer != nullptr);
        if (!localBuffer->IsEmpty()) {
            auto *tempBuffer = allocator->New<PandaVector<mem::CardTable::CardPtr>>();
            while (!localBuffer->IsEmpty()) {
                tempBuffer->push_back(localBuffer->Pop());
            }
            updateRemsetWorker_->AddPostBarrierBuffer(tempBuffer);
        }
        if (keepBuffers == mem::BuffersKeepingFlag::DELETE) {
            thread->ResetG1PostBarrierBuffer();
            allocator->Delete(localBuffer);
        }
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::OnThreadCreate(ManagedThread *thread)
{
    // Any access to other threads' data (including MAIN's) might cause a race here
    // so don't do this please.
    thread->SetPreWrbEntrypoint(reinterpret_cast<void *>(currentPreWrbEntrypoint_));
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::PreZygoteFork()
{
    GC::PreZygoteFork();
    this->DestroyWorkersTaskPool();
    this->DisableWorkerThreads();
    updateRemsetWorker_->DestroyWorker();
    // don't use thread while we are in zygote
    updateRemsetWorker_->SetUpdateConcurrent(false);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::PostZygoteFork()
{
    this->EnableWorkerThreads();
    this->CreateWorkersTaskPool();
    GC::PostZygoteFork();
    // use concurrent-option after zygote
    updateRemsetWorker_->SetUpdateConcurrent(this->GetSettings()->G1EnableConcurrentUpdateRemset());
    updateRemsetWorker_->CreateWorker();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::DrainSatb(GCAdaptiveStack *objectStack)
{
    ScopedTiming scopedTiming(__FUNCTION__, *this->GetTiming());
    // Process satb buffers of the active threads
    auto callback = [this, objectStack](ManagedThread *thread) {
        // Acquire lock here to avoid data races with the threads
        // which are terminating now.
        // Data race is happens in thread.pre_buf_. The terminating thread may
        // release own pre_buf_ while GC thread iterates over threads and gets theirs
        // pre_buf_.
        os::memory::LockHolder lock(satbAndNewobjBufLock_);
        auto preBuff = thread->GetPreBuff();
        if (preBuff == nullptr) {
            // This can happens when the thread gives us own satb_buffer but
            // doesn't unregister from ThreadManaged.
            // At this perion GC can happen and we get pre_buff null here.
            return true;
        }
        for (auto obj : *preBuff) {
            if (marker_.MarkIfNotMarked(obj)) {
                objectStack->PushToStack(RootType::SATB_BUFFER, obj);
            }
        }
        preBuff->clear();
        return true;
    };
    this->GetPandaVm()->GetThreadManager()->EnumerateThreads(callback);

    // Process satb buffers of the terminated threads
    os::memory::LockHolder lock(satbAndNewobjBufLock_);
    for (auto objVector : satbBuffList_) {
        ASSERT(objVector != nullptr);
        for (auto obj : *objVector) {
            if (marker_.MarkIfNotMarked(obj)) {
                objectStack->PushToStack(RootType::SATB_BUFFER, obj);
            }
        }
        this->GetInternalAllocator()->Delete(objVector);
    }
    satbBuffList_.clear();
    for (auto obj : newobjBuffer_) {
        if (marker_.MarkIfNotMarked(obj)) {
            objectStack->PushToStack(RootType::SATB_BUFFER, obj);
        }
    }
    newobjBuffer_.clear();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::HandlePendingDirtyCards()
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());
    updateRemsetWorker_->DrainAllCards(&dirtyCards_);
    std::for_each(dirtyCards_.cbegin(), dirtyCards_.cend(), [](auto card) { card->Clear(); });
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ReenqueueDirtyCards()
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());
    os::memory::LockHolder lock(queueLock_);
    std::for_each(dirtyCards_.cbegin(), dirtyCards_.cend(), [this](auto card) {
        card->Mark();
        updatedRefsQueue_->push_back(card);
    });
    dirtyCards_.clear();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ClearSatb()
{
    ScopedTiming scopedTiming(__FUNCTION__, *this->GetTiming());
    // Acquire lock here to avoid data races with the threads
    // which are terminating now.
    // Data race is happens in thread.pre_buf_. The terminating thread may
    // release own pre_buf_ while GC thread iterates over threads and gets theirs
    // pre_buf_.
    os::memory::LockHolder lock(satbAndNewobjBufLock_);
    // Process satb buffers of the active threads
    auto threadCallback = [](ManagedThread *thread) {
        auto preBuff = thread->GetPreBuff();
        if (preBuff != nullptr) {
            preBuff->clear();
        }
        return true;
    };
    this->GetPandaVm()->GetThreadManager()->EnumerateThreads(threadCallback);

    // Process satb buffers of the terminated threads
    for (auto objVector : satbBuffList_) {
        this->GetInternalAllocator()->Delete(objVector);
    }
    satbBuffList_.clear();
    newobjBuffer_.clear();
}

template <class LanguageConfig>
template <class Visitor>
void G1GC<LanguageConfig>::VisitRemSets(const Visitor &visitor)
{
    GCScope<TRACE_TIMING> visitRemsetScope(__FUNCTION__, this);

    ASSERT(uniqueCardsInitialized_);
    // Iterate over stored references to the collection set
    for (auto &entryVector : uniqueRefsFromRemsets_) {
        for (auto &entry : *entryVector) {
            ObjectHeader *object = entry.GetObject();
            uint32_t offset = entry.GetReferenceOffset();
            visitor(object, ObjectAccessor::GetObject(object, offset), offset);
        }
    }
}

template <class LanguageConfig>
template <class Visitor>
void G1GC<LanguageConfig>::UpdateRefsFromRemSets(const Visitor &visitor)
{
    auto fieldVisitor = [this, &visitor](ObjectHeader *object, ObjectHeader *field, uint32_t offset,
                                         [[maybe_unused]] bool isVolatile) {
        if (!InGCSweepRange(field)) {
            return true;
        }
        visitor(object, ObjectAccessor::GetObject(object, offset), offset);
        return true;
    };
    auto refsChecker = [this, &fieldVisitor](const MemRange &memRange, Region *region) {
        IterateOverRefsInMemRange(memRange, region, fieldVisitor);
        return true;
    };
    MemRange dirtyCards = CacheRefsFromRemsets(refsChecker);
    ClearDirtyAndYoungCards(dirtyCards);
}

template <class LanguageConfig>
MemRange G1GC<LanguageConfig>::CacheRefsFromRemsets(const MemRangeRefsChecker &refsChecker)
{
    GCScope<TRACE_TIMING> cacheRefsFromRemsetScope(__FUNCTION__, this);
    // Collect only unique objects to not proceed them more than once.
    ASSERT(!uniqueCardsInitialized_);
    CardTable *cardTable = this->GetCardTable();
    uintptr_t minDirtyAddr = cardTable->GetMinAddress() + cardTable->GetCardsCount() * cardTable->GetCardSize();
    uintptr_t maxDirtyAddr = cardTable->GetMinAddress();

    ASSERT(IsCardTableClear(cardTable));
    auto visitor = [cardTable, &minDirtyAddr, &maxDirtyAddr, &refsChecker](Region *r, const MemRange &range) {
        // Use the card table to mark the ranges we already processed.
        // Each card is uint8_t. Use it as a bitmap. Set bit means the corresponding memory
        // range is processed.
        CardTable::CardPtr card = cardTable->GetCardPtr(range.GetStartAddress());
        uintptr_t cardAddr = cardTable->GetCardStartAddress(card);
        size_t memSize = DEFAULT_REGION_SIZE / RemSet<>::Bitmap::GetNumBits();
        size_t bitIdx = (range.GetStartAddress() - cardAddr) / memSize;
        if ((card->GetCard() & (1U << bitIdx)) == 0) {
            card->SetCard(card->GetCard() | (1U << bitIdx));
            if (minDirtyAddr > cardAddr) {
                minDirtyAddr = cardAddr;
            }
            if (maxDirtyAddr < cardAddr + cardTable->GetCardSize()) {
                maxDirtyAddr = cardAddr + cardTable->GetCardSize();
            }
            return refsChecker(range, r);
        }
        // some cross region refs might be not processed
        return false;
    };
    analytics_.ReportScanRemsetStart(panda::time::GetCurrentTimeInNanos());
    for (auto region : collectionSet_) {
        region->GetRemSet()->Iterate(RemsetRegionPredicate, visitor);
    }
    analytics_.ReportScanRemsetEnd(panda::time::GetCurrentTimeInNanos());

    if (!this->IsFullGC()) {
        CacheRefsFromDirtyCards(visitor);
#ifndef NDEBUG
        uniqueCardsInitialized_ = true;
#endif  // NDEBUG
    }
    if (minDirtyAddr > maxDirtyAddr) {
        minDirtyAddr = maxDirtyAddr;
    }
    return MemRange(minDirtyAddr, maxDirtyAddr);
}

template <class LanguageConfig>
template <typename Visitor>
void G1GC<LanguageConfig>::CacheRefsFromDirtyCards(Visitor visitor)
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());
    auto cardTable = this->GetCardTable();
    constexpr size_t MEM_SIZE = DEFAULT_REGION_SIZE / RemSet<>::Bitmap::GetNumBits();
    for (auto it = dirtyCards_.cbegin(); it != dirtyCards_.cend();) {
        auto range = cardTable->GetMemoryRange(*it);
        auto addr = range.GetStartAddress();
        ASSERT_DO(IsHeapSpace(PoolManager::GetMmapMemPool()->GetSpaceTypeForAddr(ToVoidPtr(addr))),
                  std::cerr << "Invalid space type for the " << addr << std::endl);
        auto endAddr = range.GetEndAddress();
        auto region = panda::mem::AddrToRegion(ToVoidPtr(addr));
        if (!RemsetRegionPredicate(region)) {
            it = dirtyCards_.erase(it);
            continue;
        }

        auto allCrossRegionRefsProcessed = true;
        while (addr < endAddr) {
            if (!visitor(region, MemRange(addr, addr + MEM_SIZE))) {
                allCrossRegionRefsProcessed = false;
            }
            addr += MEM_SIZE;
        }
        if (allCrossRegionRefsProcessed) {
            it = dirtyCards_.erase(it);
            continue;
        }
        ++it;
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::RestoreYoungCards(const CollectionSet &collectionSet)
{
    CardTable *cardTable = this->GetCardTable();
    for (Region *region : collectionSet.Young()) {
        cardTable->MarkCardsAsYoung(MemRange(region->Begin(), region->End()));
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ClearYoungCards(const CollectionSet &collectionSet)
{
    CardTable *cardTable = this->GetCardTable();
    for (Region *region : collectionSet.Young()) {
        cardTable->ClearCardRange(ToUintPtr(region), ToUintPtr(region) + DEFAULT_REGION_SIZE);
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ClearDirtyAndYoungCards(const MemRange &dirtyCardsRange)
{
    CardTable *cardTable = this->GetCardTable();
    ClearYoungCards(collectionSet_);
    cardTable->ClearCardRange(dirtyCardsRange.GetStartAddress(), dirtyCardsRange.GetEndAddress());
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ClearRefsFromRemsetsCache()
{
    ASSERT(!uniqueRefsFromRemsets_.empty());
    // Resize list of unique refs from remset to 1, to reduce memory usage
    size_t elemetsToRemove = uniqueRefsFromRemsets_.size() - 1;
    for (size_t i = 0; i < elemetsToRemove; i++) {
        RefVector *entry = uniqueRefsFromRemsets_.back();
        this->GetInternalAllocator()->Delete(entry);
        uniqueRefsFromRemsets_.pop_back();
    }
    ASSERT(uniqueRefsFromRemsets_.size() == 1);
    uniqueRefsFromRemsets_.front()->clear();
    ASSERT(uniqueRefsFromRemsets_.front()->capacity() == MAX_REFS);
#ifndef NDEBUG
    uniqueCardsInitialized_ = false;
#endif  // NDEBUG
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ActualizeRemSets()
{
    ScopedTiming t(__FUNCTION__, *this->GetTiming());

    // Invalidate regions from collection set in all remsets
    for (Region *region : collectionSet_.Young()) {
        if (!region->HasFlag(RegionFlag::IS_PROMOTED)) {
            RemSet<>::template InvalidateRegion<false>(region);
        } else {
            region->RmvFlag(RegionFlag::IS_PROMOTED);
        }
    }
    for (Region *region : collectionSet_.Tenured()) {
        RemSet<>::template InvalidateRegion<false>(region);
    }
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::ShouldRunTenuredGC(const GCTask &task)
{
    return this->IsOnPygoteFork() || task.reason == GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE ||
           task.reason == GCTaskCause::STARTUP_COMPLETE_CAUSE;
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::OnWaitForIdleFail()
{
    if (this->GetGCPhase() == GCPhase::GC_PHASE_MARK) {
        // Atomic with release order reason: write to this variable should become visible in concurrent marker check
        interruptConcurrentFlag_.store(true, std::memory_order_release);
        if (this->GetSettings()->G1EnablePauseTimeGoal()) {
            os::memory::LockHolder lh(concurrentMarkMutex_);
            concurrentMarkCondVar_.Signal();
        }
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::PostponeGCStart()
{
    regionGarbageRateThreshold_ = 0;
    g1PromotionRegionAliveRate_ = 0;
    GC::PostponeGCStart();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::PostponeGCEnd()
{
    ASSERT(!this->IsPostponeEnabled() || (regionGarbageRateThreshold_ == 0 && g1PromotionRegionAliveRate_ == 0));
    regionGarbageRateThreshold_ = this->GetSettings()->G1RegionGarbageRateThreshold();
    g1PromotionRegionAliveRate_ = this->GetSettings()->G1PromotionRegionAliveRate();
    GC::PostponeGCEnd();
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::IsPostponeGCSupported() const
{
    return true;
}

template <class LanguageConfig>
size_t G1GC<LanguageConfig>::GetMaxMixedRegionsCount()
{
    return this->GetG1ObjectAllocator()->GetMaxYoungRegionsCount() + numberOfMixedTenuredRegions_;
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::PrepareYoungRegionsForFullGC(const CollectionSet &collectionSet)
{
    BuildCrossYoungRemSets(collectionSet.Young());
    ClearYoungCards(collectionSet);
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::RestoreYoungRegionsAfterFullGC(const CollectionSet &collectionSet)
{
    RestoreYoungCards(collectionSet);
    for (Region *region : collectionSet.Young()) {
        RemSet<>::template InvalidateRefsFromRegion<false>(region);
    }
}

template <class LanguageConfig>
template <typename Container>
void G1GC<LanguageConfig>::BuildCrossYoungRemSets(const Container &young)
{
    ScopedTiming scopedTiming(__FUNCTION__, *this->GetTiming());
    ASSERT(this->IsFullGC());
    auto allocator = this->GetG1ObjectAllocator();
    size_t regionSizeBits = panda::helpers::math::GetIntLog2(allocator->GetRegionSize());
    auto updateRemsets = [regionSizeBits](ObjectHeader *object, ObjectHeader *ref, size_t offset,
                                          [[maybe_unused]] bool isVolatile) {
        if (!IsSameRegion(object, ref, regionSizeBits) && !ObjectToRegion(ref)->IsYoung()) {
            RemSet<>::AddRefWithAddr<false>(object, offset, ref);
        }
        return true;
    };
    for (Region *region : young) {
        region->GetMarkBitmap()->IterateOverMarkedChunks([&updateRemsets](void *addr) {
            ObjectHelpers<LanguageConfig::LANG_TYPE>::template TraverseAllObjectsWithInfo<false>(
                reinterpret_cast<ObjectHeader *>(addr), updateRemsets);
        });
    }
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::StartConcurrentScopeRoutine() const
{
    updateRemsetWorker_->ResumeWorkerAfterGCPause();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::EndConcurrentScopeRoutine() const
{
    updateRemsetWorker_->SuspendWorkerForGCPause();
}

template <class LanguageConfig>
void G1GC<LanguageConfig>::ComputeNewSize()
{
    if (this->GetSettings()->G1EnablePauseTimeGoal()) {
        auto desiredEdenLengthByPauseDelay = CalculateDesiredEdenLengthByPauseDelay();
        auto desiredEdenLengthByPauseDuration = CalculateDesiredEdenLengthByPauseDuration();
        auto desiredEdenLength = std::max(desiredEdenLengthByPauseDelay, desiredEdenLengthByPauseDuration);
        GetG1ObjectAllocator()->GetHeapSpace()->UpdateSize(desiredEdenLength * GetG1ObjectAllocator()->GetRegionSize());
        GetG1ObjectAllocator()->SetDesiredEdenLength(desiredEdenLength);
    } else {
        GenerationalGC<LanguageConfig>::ComputeNewSize();
    }
}

template <class LanguageConfig>
size_t G1GC<LanguageConfig>::CalculateDesiredEdenLengthByPauseDelay()
{
    auto delayBeforePause = g1PauseTracker_.MinDelayBeforeMaxPauseInMicros(panda::time::GetCurrentTimeInMicros());
    return static_cast<size_t>(ceil(analytics_.PredictAllocationRate() * delayBeforePause));
}

template <class LanguageConfig>
size_t G1GC<LanguageConfig>::CalculateDesiredEdenLengthByPauseDuration()
{
    // Atomic with relaxed order reason: data race with no synchronization or ordering constraints imposed
    // on other reads or writes
    if (isMixedGcRequired_.load(std::memory_order_relaxed)) {
        // Schedule next mixed collections as often as possible to maximize old regions collection
        return 1;
    }

    // Calculate desired_eden_size according to pause time goal
    size_t minEdenLength = 1;
    size_t maxEdenLength =
        GetG1ObjectAllocator()->GetHeapSpace()->GetMaxYoungSize() / GetG1ObjectAllocator()->GetRegionSize();

    auto predictionError = analytics_.EstimatePredictionErrorInMicros();
    auto maxPause =
        static_cast<int64_t>(this->GetSettings()->GetG1MaxGcPauseInMillis() * panda::os::time::MILLIS_TO_MICRO);
    auto edenLengthPredicate = [this, predictionError, maxPause](size_t edenLength) {
        if (!HaveEnoughRegionsToMove(edenLength)) {
            return false;
        }
        auto pauseTime = predictionError + analytics_.PredictYoungCollectionTimeInMicros(edenLength);
        return pauseTime <= maxPause;
    };
    if (!edenLengthPredicate(minEdenLength)) {
        return minEdenLength;
    }

    if (edenLengthPredicate(maxEdenLength)) {
        return maxEdenLength;
    }
    auto delta = (maxEdenLength - minEdenLength) / 2U;
    while (delta > 0) {
        auto edenLength = minEdenLength + delta;
        if (edenLengthPredicate(edenLength)) {
            minEdenLength = edenLength;
        } else {
            maxEdenLength = edenLength;
        }
        ASSERT(minEdenLength < maxEdenLength);
        delta = (maxEdenLength - minEdenLength) / 2U;
    }
    return minEdenLength;
}

template <class LanguageConfig>
NO_THREAD_SAFETY_ANALYSIS void G1GC<LanguageConfig>::ConcurentMarkImpl(GCMarkingStackType *objectsStack)
{
    {
        ScopedTiming t("VisitClassRoots", *this->GetTiming());
        this->VisitClassRoots([this, objectsStack](const GCRoot &gcRoot) {
            if (concMarker_.MarkIfNotMarked(gcRoot.GetObjectHeader())) {
                ASSERT(gcRoot.GetObjectHeader() != nullptr);
                objectsStack->PushToStack(RootType::ROOT_CLASS, gcRoot.GetObjectHeader());
            } else {
                LOG_DEBUG_GC << "Skip root: " << gcRoot.GetObjectHeader();
            }
        });
    }
    {
        ScopedTiming t("VisitInternalStringTable", *this->GetTiming());
        this->GetPandaVm()->VisitStringTable(
            [this, objectsStack](ObjectHeader *str) {
                if (concMarker_.MarkIfNotMarked(str)) {
                    ASSERT(str != nullptr);
                    objectsStack->PushToStack(RootType::STRING_TABLE, str);
                }
            },
            VisitGCRootFlags::ACCESS_ROOT_ALL | VisitGCRootFlags::START_RECORDING_NEW_ROOT);
    }
    // Atomic with acquire order reason: load to this variable should become visible
    while (!objectsStack->Empty() && !interruptConcurrentFlag_.load(std::memory_order_acquire)) {
        auto *object = this->PopObjectFromStack(objectsStack);
        ASSERT(concMarker_.IsMarked(object));
        ValidateObject(nullptr, object);
        auto *objectClass = object->template ClassAddr<BaseClass>();
        // We need annotation here for the FullMemoryBarrier used in InitializeClassByIdEntrypoint
        TSAN_ANNOTATE_HAPPENS_AFTER(objectClass);
        LOG_DEBUG_GC << "Current object: " << GetDebugInfoAboutObject(object);

        ASSERT(!object->IsForwarded());
        CalcLiveBytesNotAtomicallyMarkPreprocess(object, objectClass);
        concMarker_.MarkInstance(objectsStack, object, objectClass);
    }
}

template <class LanguageConfig>
bool G1GC<LanguageConfig>::Trigger(PandaUniquePtr<GCTask> task)
{
    if (this->GetSettings()->G1EnablePauseTimeGoal() &&
        g1PauseTracker_.MinDelayBeforeMaxPauseInMicros(panda::time::GetCurrentTimeInMicros()) > 0) {
        return false;
    }
    return GenerationalGC<LanguageConfig>::Trigger(std::move(task));
}

TEMPLATE_CLASS_LANGUAGE_CONFIG(G1GC);
TEMPLATE_CLASS_LANGUAGE_CONFIG(G1GCConcurrentMarker);
TEMPLATE_CLASS_LANGUAGE_CONFIG(G1GCMixedMarker);
TEMPLATE_CLASS_LANGUAGE_CONFIG(G1GCPauseMarker);

}  // namespace panda::mem
