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

#include "g1_analytics.h"
#include "libpandabase/utils/time.h"
#include "libpandabase/os/time.h"
#include "libpandabase/utils/type_converter.h"
#include "runtime/mem/gc/card_table.h"

namespace ark::mem {
G1Analytics::G1Analytics(uint64_t now) : previousYoungCollectionEnd_(now) {}

void G1Analytics::ReportEvacuatedBytes(size_t bytes)
{
    // Atomic with relaxed order reason: data race with no synchronization or ordering constraints imposed
    // on other reads or writes
    copiedBytes_.fetch_add(bytes, std::memory_order_relaxed);
}

void G1Analytics::ReportRemsetSize(size_t remsetSize, size_t remsetRefsCount)
{
    remsetSize_ = remsetSize;
    remsetRefsCount_ = remsetRefsCount;
}

void G1Analytics::ReportMarkingStart(uint64_t time)
{
    markingStart_ = time;
}

void G1Analytics::ReportMarkingEnd(uint64_t time, size_t remsetRefsCount)
{
    markingEnd_ = time;
    totalRemsetRefsCount_ = remsetRefsCount;
}

void G1Analytics::ReportScanDirtyCardsStart(uint64_t time)
{
    scanDirtyCardsStart_ = time;
}

void G1Analytics::ReportScanDirtyCardsEnd(uint64_t time, size_t dirtyCardsCount)
{
    scanDirtyCardsEnd_ = time;
    dirtyCardsCount_ = dirtyCardsCount;
}

void G1Analytics::ReportEvacuationStart(uint64_t time)
{
    evacuationStart_ = time;
}

void G1Analytics::ReportEvacuationEnd(uint64_t time)
{
    evacuationEnd_ = time;
}

void G1Analytics::ReportUpdateRefsStart(uint64_t time)
{
    updateRefsStart_ = time;
}

void G1Analytics::ReportUpdateRefsEnd(uint64_t time)
{
    updateRefsEnd_ = time;
}

void G1Analytics::ReportPromotedRegion()
{
    // Atomic with relaxed order reason: data race with no synchronization or ordering constraints imposed
    // on other reads or writes
    promotedRegions_.fetch_add(1, std::memory_order_relaxed);
}

void G1Analytics::ReportLiveObjects(size_t num)
{
    // Atomic with relaxed order reason: data race with no synchronization or ordering constraints imposed
    // on other reads or writes
    liveObjects_.fetch_add(num, std::memory_order_relaxed);
}

double G1Analytics::PredictAllocationRate() const
{
    return predictor_.Predict(allocationRateSeq_);
}

void G1Analytics::ReportCollectionStart(uint64_t time)
{
    currentYoungCollectionStart_ = time;
    copiedBytes_ = 0;
    promotedRegions_ = 0;
    liveObjects_ = 0;
    remsetSize_ = 0;
    remsetRefsCount_ = 0;
    totalRemsetRefsCount_ = 0;
}

template <typename T>
static void DumpMetric(const char *msg, T actual, T prediction)
{
    auto error = actual > 0 ? PERCENT_100_D * (prediction - actual) / actual : std::numeric_limits<double>::quiet_NaN();
    LOG(INFO, GC) << "G1Analytics metric: " << msg << " actual " << actual << " prediction " << prediction << " error "
                  << error << "%";
}

static void DumpPauseMetric(const char *msg, uint64_t actual, uint64_t prediction, uint64_t totalPause)
{
    auto error =
        totalPause > 0 ? PERCENT_100_D * (prediction - actual) / totalPause : std::numeric_limits<double>::quiet_NaN();
    LOG(INFO, GC) << "G1Analytics metric: " << msg << " actual " << actual << " prediction " << prediction << " error "
                  << error << "%";
}

void G1Analytics::ReportCollectionEnd(GCTaskCause cause, uint64_t endTime, const CollectionSet &collectionSet,
                                      bool dump)
{
    auto edenLength = collectionSet.Young().size();
    auto appTime = (currentYoungCollectionStart_ - previousYoungCollectionEnd_) / ark::os::time::MICRO_TO_NANO;
    auto allocationRate = static_cast<double>(edenLength) / appTime;
    auto pauseTime = (endTime - currentYoungCollectionStart_) / ark::os::time::MICRO_TO_NANO;

    if (dump && cause != GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE) {
        DumpMetrics(collectionSet, pauseTime, allocationRate);
    }

    allocationRateSeq_.Add(allocationRate);

    if (cause != GCTaskCause::EXPLICIT_CAUSE && edenLength == collectionSet.size() && edenLength > 0) {
        auto liveObjectsPerRegion = static_cast<double>(liveObjects_) / edenLength;
        liveObjectsSeq_.Add(liveObjectsPerRegion);

        auto evacuationTime = (evacuationEnd_ - evacuationStart_) / ark::os::time::MICRO_TO_NANO;
        auto compactedRegions = edenLength - promotedRegions_;
        if (compactedRegions > 0) {
            auto copiedBytesPerRegion = static_cast<double>(copiedBytes_) / compactedRegions;
            copiedBytesSeq_.Add(copiedBytesPerRegion);
            auto estimatedPromotionTime = EstimatePromotionTimeInMicros(promotedRegions_);
            if (evacuationTime > estimatedPromotionTime) {
                auto copyingBytesRate = static_cast<double>(copiedBytes_) / (evacuationTime - estimatedPromotionTime);
                copyingBytesRateSeq_.Add(copyingBytesRate);
            }
        }

        auto traversedObjects = liveObjects_ + totalRemsetRefsCount_;
        auto markingTime = (markingEnd_ - markingStart_) / ark::os::time::MICRO_TO_NANO;
        auto markingRate = static_cast<double>(traversedObjects) / markingTime;
        markingRateSeq_.Add(markingRate);

        auto updateRefsTime = (updateRefsEnd_ - updateRefsStart_) / ark::os::time::MICRO_TO_NANO;
        auto updateRefsRate = static_cast<double>(traversedObjects) / updateRefsTime;
        updateRefsRateSeq_.Add(updateRefsRate);

        ASSERT(edenLength != 0);
        promotionSeq_.Add(static_cast<double>(promotedRegions_) / edenLength);

        auto pauseTimeSum = markingTime + evacuationTime + updateRefsTime;
        auto otherTime = pauseTime - pauseTimeSum;
        otherSeq_.Add(otherTime);

        if (dirtyCardsCount_ > 0) {
            auto scanDirtyCardsTime = (scanDirtyCardsEnd_ - scanDirtyCardsStart_) / ark::os::time::MICRO_TO_NANO;
            scanDirtyCardsRateSeq_.Add(static_cast<double>(dirtyCardsCount_) / scanDirtyCardsTime);
        }

        if (cause != GCTaskCause::HEAP_USAGE_THRESHOLD_CAUSE) {
            // it can be too early after previous pause and skew statistics
            remsetRefsSeq_.Add(totalRemsetRefsCount_);
        }

        if (remsetSize_ > 0) {
            remsetRefsPerChunkSeq_.Add(static_cast<double>(remsetRefsCount_) / remsetSize_);
        }
    }

    previousYoungCollectionEnd_ = endTime;
}

void G1Analytics::DumpMetrics(const CollectionSet &collectionSet, uint64_t pauseTime, double allocationRate) const
{
    DumpMetric("allocation_rate", allocationRate * DEFAULT_REGION_SIZE, PredictAllocationRate() * DEFAULT_REGION_SIZE);

    auto expectedRemsetRefsCount = predictor_.Predict(remsetRefsSeq_);
    DumpMetric("total_remset_refs_count", static_cast<double>(totalRemsetRefsCount_), expectedRemsetRefsCount);
    DumpMetric("remset_refs_count", static_cast<double>(remsetRefsCount_), expectedRemsetRefsCount);

    auto edenLength = collectionSet.Young().size();
    auto predictedYoungPause = PredictYoungCollectionTimeInMicros(edenLength);
    auto liveObjectsPerRegion =
        edenLength > 0 ? static_cast<double>(liveObjects_) / edenLength : std::numeric_limits<double>::quiet_NaN();
    DumpMetric("live_objects_per_region", liveObjectsPerRegion, predictor_.Predict(liveObjectsSeq_));

    auto expectedLiveObjects = edenLength * predictor_.Predict(liveObjectsSeq_);
    DumpMetric("live_objects", static_cast<double>(liveObjects_), expectedLiveObjects);

    auto evacuationTime = (evacuationEnd_ - evacuationStart_) / ark::os::time::MICRO_TO_NANO;

    auto compactedRegions = edenLength - promotedRegions_;
    auto expectedPromotedRegions = PredictPromotedRegions(edenLength);
    auto expectedCompactedRegions = edenLength - expectedPromotedRegions;
    DumpMetric("compacted_regions", static_cast<double>(compactedRegions), expectedCompactedRegions);

    auto copiedBytesPerRegion = compactedRegions > 0 ? static_cast<double>(copiedBytes_) / compactedRegions : 0;
    DumpMetric("copied_bytes_per_region", copiedBytesPerRegion, predictor_.Predict(copiedBytesSeq_));

    auto promotionTime =
        promotedRegions_ == edenLength ? evacuationTime : EstimatePromotionTimeInMicros(promotedRegions_);
    auto copyingTime = evacuationTime > promotionTime ? evacuationTime - promotionTime : 0;

    if (copyingTime > 0) {
        auto copyingBytesRate = static_cast<double>(copiedBytes_) / copyingTime;
        DumpMetric("copying_bytes_rate", copyingBytesRate, predictor_.Predict(copyingBytesRateSeq_));
    }

    auto expectedPromotionTime = EstimatePromotionTimeInMicros(expectedPromotedRegions);
    DumpPauseMetric("promotion_time", promotionTime, expectedPromotionTime, pauseTime);

    auto expectedCopiedBytes = expectedCompactedRegions * predictor_.Predict(copiedBytesSeq_);
    auto expectedCopyingTime = PredictCopyingTimeInMicros(expectedCopiedBytes);
    DumpPauseMetric("copying_time", copyingTime, expectedCopyingTime, pauseTime);
    DumpPauseMetric("evacuation_time", evacuationTime, expectedCopyingTime + expectedPromotionTime, pauseTime);

    auto traversedObjects = liveObjects_ + remsetRefsCount_;
    auto markingTime = (markingEnd_ - markingStart_) / ark::os::time::MICRO_TO_NANO;
    auto markingRate = static_cast<double>(traversedObjects) / markingTime;
    DumpMetric("marking_rate", markingRate, predictor_.Predict(markingRateSeq_));
    auto expectedMarkingTime = PredictMarkingTimeInMicros(expectedLiveObjects, expectedRemsetRefsCount);
    DumpPauseMetric("marking_time", markingTime, expectedMarkingTime, pauseTime);

    auto updateRefsTime = (updateRefsEnd_ - updateRefsStart_) / ark::os::time::MICRO_TO_NANO;
    auto updateRefsRate = static_cast<double>(traversedObjects) / updateRefsTime;
    DumpMetric("update_refs_rate", updateRefsRate, predictor_.Predict(updateRefsRateSeq_));
    auto expectedUpdateRefsTime = PredictUpdateRefsTimeInMicros(expectedLiveObjects, expectedRemsetRefsCount);
    DumpPauseMetric("update_refs_time", updateRefsTime, expectedUpdateRefsTime, pauseTime);

    auto otherTime = pauseTime - markingTime - evacuationTime - updateRefsTime;
    DumpPauseMetric("other_time", otherTime, static_cast<uint64_t>(predictor_.Predict(otherSeq_)), pauseTime);

    DumpMetric("young_pause_time", pauseTime, predictedYoungPause);
    if (edenLength < collectionSet.size()) {
        DumpMetric("mixed_pause_time", pauseTime, predictedMixedPause_);
    }
}

uint64_t G1Analytics::PredictYoungCollectionTimeInMicros(size_t edenLength) const
{
    auto expectedPromotedRegions = PredictPromotedRegions(edenLength);
    auto expectedCompactedRegions = edenLength - expectedPromotedRegions;
    auto expectedCopiedBytes = expectedCompactedRegions * predictor_.Predict(copiedBytesSeq_);
    auto expectedLiveObjects = edenLength * predictor_.Predict(liveObjectsSeq_);
    auto expectedRemsetRefsCount = predictor_.Predict(remsetRefsSeq_);
    auto otherTime = predictor_.Predict(otherSeq_);
    return PredictMarkingTimeInMicros(expectedLiveObjects, expectedRemsetRefsCount) +
           PredictCopyingTimeInMicros(expectedCopiedBytes) +
           PredictUpdateRefsTimeInMicros(expectedLiveObjects, expectedRemsetRefsCount) +
           EstimatePromotionTimeInMicros(expectedPromotedRegions) + otherTime;
}

uint64_t G1Analytics::PredictYoungCollectionTimeInMicros(const CollectionSet &collectionSet) const
{
    ASSERT(collectionSet.Young().size() == collectionSet.size());
    auto edenLength = collectionSet.Young().size();
    auto expectedPromotedRegions = PredictPromotedRegions(edenLength);
    auto expectedCompactedRegions = edenLength - expectedPromotedRegions;
    auto expectedCopiedBytes = expectedCompactedRegions * predictor_.Predict(copiedBytesSeq_);
    auto expectedLiveObjects = edenLength * predictor_.Predict(liveObjectsSeq_);
    size_t remsetSize = 0;
    std::for_each(collectionSet.begin(), collectionSet.end(),
                  [&remsetSize](auto *region) { remsetSize += region->GetRemSetSize(); });
    auto expectedRemsetRefsCount = PredictRemsetRefsCount(remsetSize);
    auto otherTime = predictor_.Predict(otherSeq_);
    return PredictMarkingTimeInMicros(expectedLiveObjects, expectedRemsetRefsCount) +
           PredictCopyingTimeInMicros(expectedCopiedBytes) +
           PredictUpdateRefsTimeInMicros(expectedLiveObjects, expectedRemsetRefsCount) +
           EstimatePromotionTimeInMicros(expectedPromotedRegions) + otherTime;
}

uint64_t G1Analytics::PredictOldCollectionTimeInMicros(Region *region) const
{
    auto expectedLiveObjects = region->GetLiveBytes() * region->GetAllocatedObjects() / region->GetAllocatedBytes();
    return PredictOldCollectionTimeInMicros(region->GetRemSetSize(), region->GetLiveBytes(), expectedLiveObjects);
}

uint64_t G1Analytics::PredictOldCollectionTimeInMicros(size_t remsetSize, size_t liveBytes, size_t liveObjects) const
{
    auto expectedRemsetRefsCount = PredictRemsetRefsCount(remsetSize);
    return PredictMarkingTimeInMicros(liveObjects, expectedRemsetRefsCount) + PredictCopyingTimeInMicros(liveBytes);
}
uint64_t G1Analytics::PredictScanDirtyCardsTime(size_t dirtyCardsCount) const
{
    if (dirtyCardsCount == 0) {
        return 0;
    }
    return PredictTime(dirtyCardsCount, scanDirtyCardsRateSeq_);
}

size_t G1Analytics::PredictRemsetRefsCount(size_t remsetSize) const
{
    return predictor_.Predict(remsetRefsPerChunkSeq_) * remsetSize;
}

double G1Analytics::PredictPromotedRegions(size_t edenLength) const
{
    return predictor_.Predict(promotionSeq_) * edenLength;
}

uint64_t G1Analytics::EstimatePromotionTimeInMicros(size_t promotedRegions) const
{
    return promotionCost_ * promotedRegions;
}

uint64_t G1Analytics::PredictUpdateRefsTimeInMicros(size_t liveObjects, size_t remsetRefsCount) const
{
    return PredictTime(liveObjects + remsetRefsCount, updateRefsRateSeq_);
}

uint64_t G1Analytics::PredictMarkingTimeInMicros(size_t liveObjects, size_t remsetRefsCount) const
{
    return PredictTime(liveObjects + remsetRefsCount, markingRateSeq_);
}

uint64_t G1Analytics::PredictCopyingTimeInMicros(size_t copiedBytes) const
{
    return PredictTime(copiedBytes, copyingBytesRateSeq_);
}
}  // namespace ark::mem
