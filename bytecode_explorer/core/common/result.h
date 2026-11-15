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

#ifndef BYTECODE_EXPLORER_CORE_COMMON_RESULT_H
#define BYTECODE_EXPLORER_CORE_COMMON_RESULT_H

#include <string>
#include <utility>
#include <variant>

namespace panda::bytecode_explorer {

enum class ErrorCode {
    NONE,
    INVALID_ARGUMENT,
    FILE_NOT_FOUND,
    INVALID_FORMAT,
    UNSUPPORTED_FORMAT,
    BACKEND_ERROR,
    CANCELLED,
    PLATFORM_ERROR,
};

class Status {
public:
    Status() = default;
    Status(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

    ErrorCode Code() const
    {
        return code_;
    }

    const std::string &Message() const
    {
        return message_;
    }

private:
    ErrorCode code_ {ErrorCode::NONE};
    std::string message_;
};

template <class T>
class Result {
public:
    static Result Success(T value)
    {
        return Result(std::move(value));
    }

    static Result Failure(Status error)
    {
        return Result(std::move(error));
    }

    bool HasValue() const
    {
        return std::holds_alternative<T>(storage_);
    }

    T &Value()
    {
        return std::get<T>(storage_);
    }

    const T &Value() const
    {
        return std::get<T>(storage_);
    }

    const Status &Error() const
    {
        return std::get<Status>(storage_);
    }

private:
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(Status error) : storage_(std::move(error)) {}

    std::variant<T, Status> storage_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_COMMON_RESULT_H
