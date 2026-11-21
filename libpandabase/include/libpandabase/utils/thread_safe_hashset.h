/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#ifndef LIBPANDABASE_UTILS_THREAD_SAFE_HASHSET_H
#define LIBPANDABASE_UTILS_THREAD_SAFE_HASHSET_H

#include "macros.h"
#include "os/mutex.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace panda {

/**
 * @brief Thread-safe unordered set with bucket-granular read/write locking.
 *
 * The set is partitioned into a fixed power-of-two number of buckets. Each
 * bucket owns a chain of elements and an RWLock, so readers on different
 * buckets proceed concurrently and writers only block the bucket they touch.
 * All methods can be called from multiple threads simultaneously.
 *
 * Elements are stored as individually allocated nodes, so pointers returned by
 * InsertOrGet stay stable for the lifetime of the set (the pool use case never
 * erases entries, which keeps the guarantee unconditional there).
 *
 * NOTE: Size() and Empty() acquire bucket locks one at a time and never hold
 * two locks simultaneously, therefore the observed count may be slightly
 * stale if the container is concurrently modified.
 */
template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class ThreadSafeHashset {
public:
    explicit ThreadSafeHashset(size_t bucket_count = DEFAULT_BUCKET_COUNT)
    {
        size_t count = RoundUpToPowerOfTwo(bucket_count == 0 ? DEFAULT_BUCKET_COUNT : bucket_count);
        buckets_.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            buckets_.push_back(std::make_unique<Bucket>());
        }
        bucket_mask_ = count - 1;
    }

    NO_COPY_SEMANTIC(ThreadSafeHashset);
    NO_MOVE_SEMANTIC(ThreadSafeHashset);
    ~ThreadSafeHashset() = default;

    /**
     * @brief Inserts the key if absent and returns a stable pointer to the stored element.
     *
     * The returned pointer stays valid until the element is deleted or the set is
     * destroyed; for intern-pool usage (insert-only) it is valid for the process
     * lifetime.
     */
    const Key *InsertOrGet(const Key &key)
    {
        auto &bucket = buckets_[GetBucketIndex(key)];
        os::memory::WriteLockHolder holder(bucket->lock);
        for (const auto &entry : bucket->chain) {
            if (key_equal_func_(*entry, key)) {
                return entry.get();
            }
        }
        bucket->chain.push_back(std::make_unique<Key>(key));
        return bucket->chain.back().get();
    }

    bool Has(const Key &key) const
    {
        auto &bucket = buckets_[GetBucketIndex(key)];
        os::memory::ReadLockHolder holder(bucket->lock);
        for (const auto &entry : bucket->chain) {
            if (key_equal_func_(*entry, key)) {
                return true;
            }
        }
        return false;
    }

    bool Insert(const Key &key)
    {
        auto &bucket = buckets_[GetBucketIndex(key)];
        os::memory::WriteLockHolder holder(bucket->lock);
        for (const auto &entry : bucket->chain) {
            if (key_equal_func_(*entry, key)) {
                return false;
            }
        }
        bucket->chain.push_back(std::make_unique<Key>(key));
        return true;
    }

    bool Delete(const Key &key)
    {
        auto &bucket = buckets_[GetBucketIndex(key)];
        os::memory::WriteLockHolder holder(bucket->lock);
        auto &chain = bucket->chain;
        for (auto it = chain.begin(); it != chain.end(); ++it) {
            if (key_equal_func_(**it, key)) {
                chain.erase(it);
                return true;
            }
        }
        return false;
    }

    void Clear()
    {
        for (const auto &bucket : buckets_) {
            os::memory::WriteLockHolder holder(bucket->lock);
            bucket->chain.clear();
        }
    }

    size_t Size() const
    {
        size_t total = 0;
        for (const auto &bucket : buckets_) {
            os::memory::ReadLockHolder holder(bucket->lock);
            total += bucket->chain.size();
        }
        return total;
    }

    bool Empty() const
    {
        return Size() == 0;
    }

private:
    struct Bucket {
        std::vector<std::unique_ptr<Key>> chain;
        mutable os::memory::RWLock lock;
    };

    static constexpr size_t DEFAULT_BUCKET_COUNT = 16384U;

    static size_t RoundUpToPowerOfTwo(size_t value)
    {
        size_t result = 1U;
        while (result < value) {
            result <<= 1U;
        }
        return result;
    }

    size_t GetBucketIndex(const Key &key) const
    {
        return hash_func_(key) & bucket_mask_;
    }

    std::vector<std::unique_ptr<Bucket>> buckets_;
    size_t bucket_mask_ {0};
    Hash hash_func_;
    KeyEqual key_equal_func_;
};

}  // namespace panda

#endif  // LIBPANDABASE_UTILS_THREAD_SAFE_HASHSET_H
