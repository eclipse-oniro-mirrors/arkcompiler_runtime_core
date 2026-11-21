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

#ifndef BYTECODE_EXPLORER_CORE_COMMON_CANCELLATION_H
#define BYTECODE_EXPLORER_CORE_COMMON_CANCELLATION_H

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace panda::bytecode_explorer {

class CancellationToken final {
public:
    CancellationToken() = default;
    explicit CancellationToken(std::function<bool()> check) : check_(std::move(check)) {}

    bool IsCancellationRequested() const
    {
        return check_ != nullptr && check_();
    }

private:
    std::function<bool()> check_;
};

class CancellationSource final {
public:
    CancellationSource() : cancelled_(std::make_shared<std::atomic<bool>>(false)) {}

    CancellationToken Token() const
    {
        auto cancelled = cancelled_;
        return CancellationToken(
            [cancelled = std::move(cancelled)] { return cancelled->load(std::memory_order_relaxed); });
    }

    void Cancel() const
    {
        cancelled_->store(true, std::memory_order_relaxed);
    }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_COMMON_CANCELLATION_H
