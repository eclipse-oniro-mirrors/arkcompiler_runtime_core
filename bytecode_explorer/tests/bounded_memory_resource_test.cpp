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

#include "core/common/bounded_memory_resource.h"
#include "core/disassembly.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <new>

namespace panda::bytecode_explorer::test {
namespace {

class RecordingMemoryResource final : public pmr::memory_resource {
public:
    size_t allocateCalls {0U};
    size_t deallocateCalls {0U};
    bool failAllocations {false};

private:
    void *do_allocate(size_t bytes, size_t alignment) override
    {
        ++allocateCalls;
        if (failAllocations) {
            throw std::bad_alloc {};
        }
        return NewDeleteResource()->allocate(bytes, alignment);
    }

    void do_deallocate(void *memory, size_t bytes, size_t alignment) override
    {
        ++deallocateCalls;
        NewDeleteResource()->deallocate(memory, bytes, alignment);
    }

    bool do_is_equal(const pmr::memory_resource &other) const noexcept override
    {
        return this == &other;
    }
};

// 64 bytes matches a common CPU cache line.
constexpr std::size_t OVER_ALIGNED_VALUE_ALIGNMENT = 64U;
struct alignas(OVER_ALIGNED_VALUE_ALIGNMENT) OverAlignedValue {
    uint8_t value {0U};
};

TEST(BoundedMemoryResourceTest, ModelContainersChargeAndReleaseBudget)
{
    BoundedMemoryResource bounded(4096U);

    {
        ModelString text(&bounded);
        ModelVector<uint32_t> values(&bounded);
        text.assign(256U, 'x');
        values.resize(128U);

        EXPECT_GT(bounded.LiveBytes(), 0U);
        EXPECT_LE(bounded.LiveBytes(), bounded.LimitBytes());
    }

    EXPECT_EQ(bounded.LiveBytes(), 0U);
    EXPECT_GT(bounded.PeakBytes(), 0U);
}

TEST(BoundedMemoryResourceTest, ModelContainerFailurePreservesAccounting)
{
    BoundedMemoryResource bounded(1U);
    ModelVector<uint64_t> values(&bounded);

    EXPECT_THROW(values.push_back(1U), ModelBudgetExceeded);
    EXPECT_EQ(bounded.LiveBytes(), 0U);
    EXPECT_EQ(bounded.PeakBytes(), 0U);
}

TEST(BoundedMemoryResourceTest, SupportsOverAlignedModelStorage)
{
    BoundedMemoryResource bounded(4096U);

    {
        ModelVector<OverAlignedValue> values(&bounded);
        values.resize(2U);
        const auto address = reinterpret_cast<uintptr_t>(values.data());

        EXPECT_EQ(address % alignof(OverAlignedValue), 0U);
        EXPECT_GE(bounded.LiveBytes(), 2U * sizeof(OverAlignedValue));
    }

    EXPECT_EQ(bounded.LiveBytes(), 0U);
}

TEST(BoundedMemoryResourceTest, RejectsBeforeCallingUpstreamAndReleasesToZero)
{
    RecordingMemoryResource upstream;
    BoundedMemoryResource bounded(16U, &upstream);

    auto *first = bounded.allocate(16U);

    EXPECT_EQ(upstream.allocateCalls, 1U);
    EXPECT_EQ(bounded.LiveBytes(), 16U);
    EXPECT_EQ(bounded.PeakBytes(), 16U);
    EXPECT_THROW(static_cast<void>(bounded.allocate(1U)), ModelBudgetExceeded);
    EXPECT_EQ(upstream.allocateCalls, 1U);
    bounded.deallocate(first, 16U);
    EXPECT_EQ(upstream.deallocateCalls, 1U);
    EXPECT_EQ(bounded.LiveBytes(), 0U);
    EXPECT_EQ(bounded.PeakBytes(), 16U);
}

TEST(BoundedMemoryResourceTest, DoesNotTranslateUpstreamBadAlloc)
{
    RecordingMemoryResource upstream;
    upstream.failAllocations = true;
    BoundedMemoryResource bounded(16U, &upstream);
    bool sawUpstreamBadAlloc = false;

    try {
        static_cast<void>(bounded.allocate(8U));
        FAIL() << "allocation unexpectedly succeeded";
    } catch (const ModelBudgetExceeded &) {
        FAIL() << "upstream OOM was translated to a model budget failure";
    } catch (const std::bad_alloc &) {
        sawUpstreamBadAlloc = true;
    }

    EXPECT_TRUE(sawUpstreamBadAlloc);
    EXPECT_EQ(upstream.allocateCalls, 1U);
    EXPECT_EQ(bounded.LiveBytes(), 0U);
    EXPECT_EQ(bounded.PeakBytes(), 0U);
}

TEST(BoundedMemoryResourceTest, ZeroBudgetRejectsBeforeCallingUpstream)
{
    RecordingMemoryResource upstream;
    BoundedMemoryResource bounded(0U, &upstream);

    EXPECT_THROW(static_cast<void>(bounded.allocate(1U)), ModelBudgetExceeded);
    EXPECT_EQ(upstream.allocateCalls, 0U);
    EXPECT_EQ(bounded.LiveBytes(), 0U);
    EXPECT_EQ(bounded.PeakBytes(), 0U);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
