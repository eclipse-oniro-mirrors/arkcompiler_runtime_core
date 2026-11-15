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

#ifndef BYTECODE_EXPLORER_WEB_HOST_HTTP_REQUEST_H
#define BYTECODE_EXPLORER_WEB_HOST_HTTP_REQUEST_H

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace panda::bytecode_explorer {

struct HttpRequestLimits {
    static constexpr size_t MAX_HEADER_BYTES = 16U * 1024U;
    static constexpr size_t MAX_BODY_BYTES = 1024U * 1024U;
    static constexpr size_t MAX_REQUEST_BYTES = MAX_HEADER_BYTES + MAX_BODY_BYTES;
    static constexpr size_t MAX_TARGET_BYTES = 2048U;
    static constexpr size_t MAX_HEADER_LINE_BYTES = 4096U;
    static constexpr size_t MAX_HEADER_COUNT = 64U;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string body;
    std::unordered_map<std::string, std::string> headers;

    std::string_view Header(std::string_view lowerCaseName) const;
};

enum class HttpParseStatus {
    COMPLETE,
    INCOMPLETE,
    INVALID,
    TOO_LARGE,
};

struct HttpParseResult {
    HttpParseStatus status {HttpParseStatus::INCOMPLETE};
    HttpRequest request;
    std::string diagnostic;
};

HttpParseResult ParseHttpRequest(std::string_view bytes);
bool SecureTokenEquals(std::string_view left, std::string_view right);

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_WEB_HOST_HTTP_REQUEST_H
