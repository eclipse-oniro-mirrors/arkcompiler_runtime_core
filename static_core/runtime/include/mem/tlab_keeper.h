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

#ifndef PANDA_RUNTIME_MEM_TLAB_KEEPER_H
#define PANDA_RUNTIME_MEM_TLAB_KEEPER_H

#include <atomic>
#include "libarkbase/macros.h"

namespace ark::mem {

class TLAB;
class WeightedAdaptiveTlabAverage;

/**
 * @brief Class used to keep tlabs per std::thread
 *
 * Stores current tlab for OS thread and used for sharing tlabs between mutators executing on the same OS thread.
 * Tlab keeper also works as shared ptr between mutators for tlab and mutators must increment and decrement counter of
 * tlabKeeper
 *
 * OS Thread A:
 *   Mutator A1 ─┐
 *   Mutator A2 ─┼─> TLABKeeper A (thread local static variable)
 *   Mutator A3 ─┘         └────> TLAB A
 *
 * OS Thread B:
 *   Mutator B1 ───> TLABKeeper B ───> TLAB B
 *
 * @note Mutator use its own reference to the tlab for allocations and uses tlabKeeper's tlab only when mutator's tlab
 * is full. In that case, if tlab of the keeper is not full mutator updates its reference to the tlab of keeper. If
 * keeper's tlab is full too both new tlab shall be created and stored in both mutator and keeper
 *
 * @note If mutator changes its OS thread it must also reset its references for both tlab and tlabKeeper.
 */
class TLABKeeper {
public:
    /// @brief Increment counter of current tlabKeeper as shared ptr and return reference to it
    static TLABKeeper *IncreaseRefCounterAndGetCurrent();

    /**
     * @brief Returns reference to current tlabKeeper
     * @note If you want to store this reference, please use IncreaseRefCounterAndGetCurrent and DecreaseRefCounter
     */
    static TLABKeeper *GetCurrent()
    {
        return &instance_;
    }

    /**
     * @brief Set up tlab to be kept in the keeper
     * @param TLAB *tlab - tlab to be kept
     */
    void SetTLAB(TLAB *tlab);

    /// @brief Creates if needed and return tlab statistics for kept tlab
    WeightedAdaptiveTlabAverage *GetWeightedTlabAverage();

    /// @brief Returns kept tlab
    TLAB *GetTLAB()
    {
        return tlab_;
    }

    /// @brief Returns kept tlab
    const TLAB *GetTLAB() const
    {
        return tlab_;
    }

    /**
     * @brief Decreases counter of keeper. Calls callback if counter is zero
     * @param callback - callback to be called when counter is zero. Signature must be void(TLAB*). Leave nullptr for no
     * callback
     */
    template <class F = std::nullptr_t>
    void DecreaseRefCounter(const F &callback = nullptr)
    {
        // Atomic with acq_rel order reason: sync with other ReturnReference from other threads
        auto oldVal = counter_.fetch_sub(1, std::memory_order_acq_rel);
        if (oldVal == 1) {
            if (tlab_ != nullptr) {
                if constexpr (std::is_invocable_v<F, TLAB *>) {
                    callback(tlab_);
                }
                SetTLAB(nullptr);
            } else {
                DeleteTlabMetrics();
            }
        } else {
            ASSERT(oldVal > 0);
        }
    }

    NO_COPY_SEMANTIC(TLABKeeper);
    NO_MOVE_SEMANTIC(TLABKeeper);
    TLABKeeper() = default;
    ~TLABKeeper() = default;

private:
    TLAB *tlab_ = nullptr;
    // Decrementing of counter could be done from other threads and hence counter must be atomic
    std::atomic<size_t> counter_ = 0;
    WeightedAdaptiveTlabAverage *weightedAdaptiveTlabMetrics_ = nullptr;

    void DeleteTlabMetrics();

    static thread_local TLABKeeper instance_;
};

}  // namespace ark::mem

#endif  // PANDA_RUNTIME_MEM_TLAB_KEEPER_H
