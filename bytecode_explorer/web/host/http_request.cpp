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

#include "web/host/http_request.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

HttpParseResult Error(HttpParseStatus status, std::string diagnostic)
{
    HttpParseResult result;
    result.status = status;
    result.diagnostic = std::move(diagnostic);
    return result;
}

bool IsHeaderNameCharacter(char value)
{
    const auto byte = static_cast<unsigned char>(value);
    if (std::isalnum(byte) != 0) {
        return true;
    }
    constexpr std::string_view SPECIALS = "!#$%&'*+-.^_`|~";
    return SPECIALS.find(value) != std::string_view::npos;
}

std::string Lower(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char byte) { return static_cast<char>(std::tolower(byte)); });
    return result;
}

std::string_view Trim(std::string_view value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1U);
    }
    return value;
}

bool HasInvalidValueCharacter(std::string_view value)
{
    return std::any_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return (byte < 0x20U && character != '\t') || byte == 0x7FU;
    });
}

// Parses and validates the request line into method, target, path and query.
bool ParseRequestLine(std::string_view requestLine, HttpRequest *request, HttpParseResult *error)
{
    const auto firstSpace = requestLine.find(' ');
    const auto secondSpace =
        firstSpace == std::string_view::npos ? std::string_view::npos : requestLine.find(' ', firstSpace + 1U);
    if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos ||
        requestLine.find(' ', secondSpace + 1U) != std::string_view::npos) {
        *error = Error(HttpParseStatus::INVALID, "Malformed HTTP request line");
        return false;
    }
    request->method = std::string(requestLine.substr(0U, firstSpace));
    request->target = std::string(requestLine.substr(firstSpace + 1U, secondSpace - firstSpace - 1U));
    const auto version = requestLine.substr(secondSpace + 1U);
    if ((request->method != "GET" && request->method != "POST" && request->method != "PUT") || version != "HTTP/1.1") {
        *error = Error(HttpParseStatus::INVALID, "Only HTTP/1.1 GET, POST and PUT requests are accepted");
        return false;
    }
    if (request->target.empty() || request->target.front() != '/' ||
        request->target.size() > HttpRequestLimits::MAX_TARGET_BYTES ||
        request->target.find('#') != std::string::npos || HasInvalidValueCharacter(request->target)) {
        *error = Error(HttpParseStatus::INVALID, "Invalid HTTP request target");
        return false;
    }
    const auto querySeparator = request->target.find('?');
    request->path = request->target.substr(0U, querySeparator);
    request->query = querySeparator == std::string::npos ? "" : request->target.substr(querySeparator + 1U);
    return true;
}

// Parses the header lines of one header section into the request.
bool ParseHeaderLines(std::string_view bytes, size_t requestLineEnd, size_t terminator, HttpRequest *request,
                      HttpParseResult *error)
{
    size_t cursor = requestLineEnd + 2U;
    size_t headerCount = 0U;
    while (cursor < terminator) {
        const auto lineEnd = bytes.find("\r\n", cursor);
        if (lineEnd == std::string_view::npos || lineEnd > terminator ||
            lineEnd - cursor > HttpRequestLimits::MAX_HEADER_LINE_BYTES) {
            *error = Error(HttpParseStatus::INVALID, "Invalid HTTP header line");
            return false;
        }
        if (++headerCount > HttpRequestLimits::MAX_HEADER_COUNT) {
            *error = Error(HttpParseStatus::TOO_LARGE, "HTTP request has too many headers");
            return false;
        }
        const auto line = bytes.substr(cursor, lineEnd - cursor);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0U ||
            !std::all_of(line.begin(), line.begin() + static_cast<std::ptrdiff_t>(colon), IsHeaderNameCharacter)) {
            *error = Error(HttpParseStatus::INVALID, "Invalid HTTP header name");
            return false;
        }
        auto name = Lower(line.substr(0U, colon));
        const auto value = Trim(line.substr(colon + 1U));
        if (HasInvalidValueCharacter(value) || request->headers.find(name) != request->headers.end()) {
            *error = Error(HttpParseStatus::INVALID, "Invalid or duplicate HTTP header");
            return false;
        }
        request->headers.emplace(std::move(name), std::string(value));
        cursor = lineEnd + 2U;
    }
    return true;
}

// Validates the parsed headers and body length, completing the request with its body once all
// bytes have arrived.
HttpParseResult FinalizeHttpRequest(HttpRequest request, std::string_view bytes, size_t bodyOffset)
{
    if (request.headers.find("host") == request.headers.end()) {
        return Error(HttpParseStatus::INVALID, "HTTP/1.1 Host header is required");
    }
    if (request.headers.find("transfer-encoding") != request.headers.end()) {
        return Error(HttpParseStatus::INVALID, "Transfer-Encoding is not supported");
    }
    size_t bodySize = 0U;
    const auto contentLength = request.Header("content-length");
    if (!contentLength.empty()) {
        const auto conversion =
            std::from_chars(contentLength.data(), contentLength.data() + contentLength.size(), bodySize);
        if (conversion.ec != std::errc {} || conversion.ptr != contentLength.data() + contentLength.size()) {
            return Error(HttpParseStatus::INVALID, "Content-Length must be one bounded decimal integer");
        }
    }
    if (bodySize > HttpRequestLimits::MAX_BODY_BYTES) {
        return Error(HttpParseStatus::TOO_LARGE, "HTTP request body exceeds the configured limit");
    }
    if (request.method == "GET" && bodySize != 0U) {
        return Error(HttpParseStatus::INVALID, "GET request bodies are not supported");
    }
    const size_t expectedSize = bodyOffset + bodySize;
    if (bytes.size() < expectedSize) {
        return {};
    }
    if (bytes.size() != expectedSize) {
        return Error(HttpParseStatus::INVALID, "HTTP request has trailing bytes or pipelined data");
    }
    request.body = std::string(bytes.substr(bodyOffset, bodySize));

    HttpParseResult result;
    result.status = HttpParseStatus::COMPLETE;
    result.request = std::move(request);
    return result;
}

}  // namespace

std::string_view HttpRequest::Header(std::string_view lowerCaseName) const
{
    const auto found = headers.find(std::string(lowerCaseName));
    return found == headers.end() ? std::string_view {} : std::string_view(found->second);
}

HttpParseResult ParseHttpRequest(std::string_view bytes)
{
    if (bytes.size() > HttpRequestLimits::MAX_REQUEST_BYTES) {
        return Error(HttpParseStatus::TOO_LARGE, "HTTP request exceeds the configured limit");
    }
    const auto terminator = bytes.find("\r\n\r\n");
    if (terminator == std::string_view::npos) {
        if (bytes.size() > HttpRequestLimits::MAX_HEADER_BYTES) {
            return Error(HttpParseStatus::TOO_LARGE, "HTTP request headers exceed the configured limit");
        }
        return {};
    }
    const size_t bodyOffset = terminator + 4U;
    if (bodyOffset > HttpRequestLimits::MAX_HEADER_BYTES) {
        return Error(HttpParseStatus::TOO_LARGE, "HTTP request headers exceed the configured limit");
    }

    const auto requestLineEnd = bytes.find("\r\n");
    if (requestLineEnd == std::string_view::npos || requestLineEnd > HttpRequestLimits::MAX_HEADER_LINE_BYTES) {
        return Error(HttpParseStatus::INVALID, "Invalid HTTP request line");
    }
    HttpRequest request;
    HttpParseResult error;
    if (!ParseRequestLine(bytes.substr(0U, requestLineEnd), &request, &error)) {
        return error;
    }
    if (!ParseHeaderLines(bytes, requestLineEnd, terminator, &request, &error)) {
        return error;
    }
    return FinalizeHttpRequest(std::move(request), bytes, bodyOffset);
}

bool SecureTokenEquals(std::string_view left, std::string_view right)
{
    const auto width = std::max(left.size(), right.size());
    size_t difference = left.size() ^ right.size();
    for (size_t index = 0U; index < width; ++index) {
        const auto leftByte = index < left.size() ? static_cast<uint8_t>(left[index]) : 0U;
        const auto rightByte = index < right.size() ? static_cast<uint8_t>(right[index]) : 0U;
        difference |= leftByte ^ rightByte;
    }
    return difference == 0U;
}

}  // namespace panda::bytecode_explorer
