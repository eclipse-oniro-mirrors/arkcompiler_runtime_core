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

#include "macros.h"

#include <algorithm>
#include <cstdlib>

namespace panda::bytecode_explorer {

const char *ModelBudgetExceeded::what() const noexcept
{
    return "disassembly model memory budget exceeded";
}

BoundedMemoryResource::BoundedMemoryResource(uint64_t limitBytes, pmr::memory_resource *upstream)
    : limitBytes_(limitBytes), upstream_(upstream)
{
    if (upstream_ == nullptr) {
        upstream_ = pmr::new_delete_resource();
    }
}

uint64_t BoundedMemoryResource::LiveBytes() const
{
    return liveBytes_;
}

uint64_t BoundedMemoryResource::PeakBytes() const
{
    return peakBytes_;
}

uint64_t BoundedMemoryResource::LimitBytes() const
{
    return limitBytes_;
}

void *BoundedMemoryResource::do_allocate(size_t bytes, size_t alignment)
{
    if (bytes > limitBytes_ - std::min(liveBytes_, limitBytes_)) {
        throw ModelBudgetExceeded {};
    }
    void *result = upstream_->allocate(bytes, alignment);
    liveBytes_ += bytes;
    peakBytes_ = std::max(peakBytes_, liveBytes_);
    return result;
}

void BoundedMemoryResource::do_deallocate(void *memory, size_t bytes, size_t alignment)
{
    if (bytes > liveBytes_) {
        ASSERT_PRINT(false, "BoundedMemoryResource deallocation exceeds live allocation bytes");
        std::abort();
    }
    upstream_->deallocate(memory, bytes, alignment);
    liveBytes_ -= bytes;
}

bool BoundedMemoryResource::do_is_equal(const pmr::memory_resource &other) const noexcept
{
    return this == &other;
}

}  // namespace panda::bytecode_explorer
