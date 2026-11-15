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

#ifndef BYTECODE_EXPLORER_CORE_COMMON_BOUNDED_MEMORY_RESOURCE_H
#define BYTECODE_EXPLORER_CORE_COMMON_BOUNDED_MEMORY_RESOURCE_H

#include "core/common/memory_resource.h"

#include <cstddef>
#include <cstdint>
#include <new>

namespace panda::bytecode_explorer {

class ModelBudgetExceeded final : public std::bad_alloc {
public:
    const char *what() const noexcept override;
};

class BoundedMemoryResource final : public pmr::memory_resource {
public:
    explicit BoundedMemoryResource(uint64_t limitBytes, pmr::memory_resource *upstream = pmr::new_delete_resource());

    BoundedMemoryResource(const BoundedMemoryResource &) = delete;
    BoundedMemoryResource &operator=(const BoundedMemoryResource &) = delete;
    BoundedMemoryResource(BoundedMemoryResource &&) = delete;
    BoundedMemoryResource &operator=(BoundedMemoryResource &&) = delete;
    ~BoundedMemoryResource() override = default;

    uint64_t LiveBytes() const;
    uint64_t PeakBytes() const;
    uint64_t LimitBytes() const;

private:
    void *do_allocate(size_t bytes, size_t alignment) override;
    void do_deallocate(void *memory, size_t bytes, size_t alignment) override;
    bool do_is_equal(const pmr::memory_resource &other) const noexcept override;

    uint64_t limitBytes_ {0U};
    uint64_t liveBytes_ {0U};
    uint64_t peakBytes_ {0U};
    pmr::memory_resource *upstream_ {nullptr};
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_COMMON_BOUNDED_MEMORY_RESOURCE_H
