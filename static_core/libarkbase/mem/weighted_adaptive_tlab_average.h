/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#ifndef LIBPANDABASE_MEM_WEIGHTED_ADAPTIVE_TLAB_AVERAGE_H
#define LIBPANDABASE_MEM_WEIGHTED_ADAPTIVE_TLAB_AVERAGE_H

#include <algorithm>
#include "libarkbase/macros.h"

namespace ark::mem {
class WeightedAdaptiveTlabAverage {
public:
    explicit WeightedAdaptiveTlabAverage(float initTlabSize, float upperSumBorder, float maxGrowRatio, float weight,
                                         float desiredFillFraction)
        : maxGrowRatio_(maxGrowRatio),
          lowerSumBorder_(initTlabSize),
          upperSumBorder_(upperSumBorder),
          weight_(weight),
          desiredFillFraction_(desiredFillFraction),
          lastCountedSum_(initTlabSize)
    {
        ASSERT(maxGrowRatio_ > 1.0);
        ASSERT(lowerSumBorder_ <= upperSumBorder_);
    }

    void StoreNewSample(size_t occupiedSize, size_t maxSize)
    {
        // Supposed that this method is called only in case of slow path
        ASSERT(occupiedSize <= maxSize);
        if (maxSize > 0) {
            float frac = static_cast<float>(occupiedSize) / maxSize;
            averageFillFraction_ =
                averageFillFraction_ < 0 ? frac : frac * weight_ + averageFillFraction_ * (1.F - weight_);
        }
    }

    /**
     * @brief Estimates new TLAB size using saved TLABs' information
     *
     * If vector with samples is not empty, we start estimating TLABs' average fill fraction
     * If average fill fraction is less than desiredFillFraction_, then we increase TLABs' size
     * Else, we reduce TLABs' size
     */
    void ComputeNewSumAndResetSamples()
    {
        if (averageFillFraction_ < 0) {
            // it means that zero tlab is the only sample that we currently have
            // in this case we don't have enough information to calculate new TLAB size
            return;
        }

        float newCountedSum;
        if (averageFillFraction_ < desiredFillFraction_) {
            // Should increase TLABs' size
            newCountedSum = std::clamp(lastCountedSum_ * (desiredFillFraction_ / averageFillFraction_),
                                       lastCountedSum_ / maxGrowRatio_, lastCountedSum_ * maxGrowRatio_);
        } else {
            // In this case we have few TLABs with good average fill fraction
            // It means that we should consider reducing TLABs' size
            newCountedSum = lastCountedSum_ * REDUCTION_RATE;
        }
        averageFillFraction_ = -1.F;
        // Mind lower and upper borders
        lastCountedSum_ =
            std::clamp(newCountedSum * weight_ + lastCountedSum_ * (1.0F - weight_), lowerSumBorder_, upperSumBorder_);
    }

    float GetLastCountedSum() const
    {
        return lastCountedSum_;
    }

    size_t GetLastCountedSumInSizeT() const
    {
        ASSERT(lowerSumBorder_ <= lastCountedSum_);
        ASSERT(lastCountedSum_ <= upperSumBorder_);
        return static_cast<size_t>(lastCountedSum_);
    }

private:
    static constexpr float REDUCTION_RATE = 0.75;  // Value used for size reduction
    const float maxGrowRatio_;                     // Max change ratio when new average is estimated
                                                   // (means that (newAverage / average) < maxGrowRatio_)
    const float lowerSumBorder_;                   // Min value that sum may take
    const float upperSumBorder_;                   // Max value that sum may take
    const float weight_;                           // From 0 to 1
                                                   // Used for better average estimation
                                                   // Recently observed samples should have greater weight
    const float desiredFillFraction_;              // From 0 to 1
                                                   // If (occupiedSize / maxSize) < desiredFillFraction_ then
                                                   // lastCountedSum_ should grow
    float averageFillFraction_ = -1;               // Average fill fraction of samples. Used for estimation of a new sum
    float lastCountedSum_;                         // Last estimated sum
};

}  // namespace ark::mem
#endif  // LIBPANDABASE_MEM_WEIGHTED_ADAPTIVE_TLAB_AVERAGE_H
