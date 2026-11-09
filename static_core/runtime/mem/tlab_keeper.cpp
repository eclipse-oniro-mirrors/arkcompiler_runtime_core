/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include "runtime/include/mem/tlab_keeper.h"
#include "runtime/include/runtime.h"
#include "libarkbase/mem/weighted_adaptive_tlab_average.h"
#include "runtime/mem/tlab.h"

namespace ark::mem {
thread_local TLABKeeper TLABKeeper::instance_;

/* static */
TLABKeeper *TLABKeeper::IncreaseRefCounterAndGetCurrent()
{
    auto *keeper = &instance_;
    // Atomic with relaxed order reason: no need for sync
    keeper->counter_.fetch_add(1, std::memory_order_relaxed);
    return keeper;
}

void TLABKeeper::SetTLAB(TLAB *tlab)
{
    tlab_ = tlab;
    if (tlab == nullptr) {
        DeleteTlabMetrics();
    }
}

WeightedAdaptiveTlabAverage *TLABKeeper::GetWeightedTlabAverage()
{
    if (weightedAdaptiveTlabMetrics_ == nullptr) {
        ASSERT(Runtime::GetOptions().IsAdaptiveTlabSize());
        auto allocator = Runtime::GetCurrent()->GetInternalAllocator();
        constexpr size_t MAX_GROW_RATIO = 2;
        constexpr float WEIGHT = 0.5;
        constexpr float DESIRED_FILL_FRACTION = 0.9;
        size_t initTlabSize = Runtime::GetOptions().GetInitTlabSize();
        size_t maxTlabSize = Runtime::GetOptions().GetMaxTlabSize();
        LOG_IF(UNLIKELY(initTlabSize < 4_KB), FATAL, RUNTIME) << "Initial TLAB size must be greater than 4Kb";
        LOG_IF(UNLIKELY(initTlabSize > maxTlabSize), FATAL, RUNTIME)
            << "Initial TLAB size must be less or equal to max TLAB size";
        weightedAdaptiveTlabMetrics_ = allocator->New<WeightedAdaptiveTlabAverage>(
            initTlabSize, maxTlabSize, MAX_GROW_RATIO, WEIGHT, DESIRED_FILL_FRACTION);
    }
    return weightedAdaptiveTlabMetrics_;
}

void TLABKeeper::DeleteTlabMetrics()
{
    if (weightedAdaptiveTlabMetrics_ != nullptr) {
        Runtime::GetCurrent()->GetInternalAllocator()->Delete(weightedAdaptiveTlabMetrics_);
        weightedAdaptiveTlabMetrics_ = nullptr;
    }
}

};  // namespace ark::mem
