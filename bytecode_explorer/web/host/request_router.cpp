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

#include "web/host/request_router.h"

#include "web/assets/web_assets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace panda::bytecode_explorer {
namespace {

constexpr std::string_view BEARER_PREFIX = "Bearer ";

constexpr int HTTP_STATUS_OK = 200;
constexpr int HTTP_STATUS_ACCEPTED = 202;
constexpr int HTTP_STATUS_BAD_REQUEST = 400;
constexpr int HTTP_STATUS_UNAUTHORIZED = 401;
constexpr int HTTP_STATUS_FORBIDDEN = 403;
constexpr int HTTP_STATUS_NOT_FOUND = 404;
constexpr int HTTP_STATUS_METHOD_NOT_ALLOWED = 405;
constexpr int HTTP_STATUS_CONFLICT = 409;
constexpr int HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415;
constexpr int HTTP_STATUS_UNPROCESSABLE_CONTENT = 422;
constexpr int HTTP_STATUS_INTERNAL_SERVER_ERROR = 500;
constexpr int HTTP_STATUS_SERVICE_UNAVAILABLE = 503;

constexpr int DECIMAL_RADIX = 10;
constexpr int HEXADECIMAL_RADIX = 16;
// 'a' is the first hexadecimal digit encoded by a letter, so letters start at this value.
constexpr int HEXADECIMAL_LETTER_BASE = 10;
constexpr uint64_t MAX_METADATA_PAGE_LIMIT = 200U;

std::vector<std::pair<std::string, std::string>> SecurityHeaders()
{
    return {{"Cache-Control", "no-store"},
            {"Content-Security-Policy",
             "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; "
             "object-src 'none'; base-uri 'none'; frame-ancestors 'none'"},
            {"Cross-Origin-Opener-Policy", "same-origin"},
            {"Cross-Origin-Resource-Policy", "same-origin"},
            {"Referrer-Policy", "no-referrer"},
            {"X-Content-Type-Options", "nosniff"},
            {"X-Frame-Options", "DENY"}};
}

HttpResponse Response(int status, std::string reason, std::string contentType, std::string body)
{
    return {status, std::move(reason), std::move(contentType), std::move(body), SecurityHeaders()};
}

HttpResponse JsonError(int status, std::string reason, std::string message)
{
    nlohmann::json body = {{"error", std::move(message)}, {"status", status}};
    return Response(status, std::move(reason), "application/json; charset=utf-8", body.dump());
}

HttpResponse MethodNotAllowed(std::string allow)
{
    auto response = JsonError(HTTP_STATUS_METHOD_NOT_ALLOWED, "Method Not Allowed",
                              "HTTP method is not accepted by this local route");
    response.headers.emplace_back("Allow", std::move(allow));
    return response;
}

HttpResponse ApiResult(const Result<std::string> &result)
{
    if (result.HasValue()) {
        return Response(HTTP_STATUS_OK, "OK", "application/json; charset=utf-8", result.Value());
    }
    switch (result.Error().Code()) {
        case ErrorCode::INVALID_ARGUMENT:
            return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", result.Error().Message());
        case ErrorCode::FILE_NOT_FOUND:
            return JsonError(HTTP_STATUS_NOT_FOUND, "Not Found", result.Error().Message());
        case ErrorCode::INVALID_FORMAT:
        case ErrorCode::UNSUPPORTED_FORMAT:
            return JsonError(HTTP_STATUS_UNPROCESSABLE_CONTENT, "Unprocessable Content", result.Error().Message());
        case ErrorCode::CANCELLED:
            return JsonError(HTTP_STATUS_CONFLICT, "Conflict", result.Error().Message());
        case ErrorCode::PLATFORM_ERROR:
        case ErrorCode::BACKEND_ERROR:
        case ErrorCode::NONE:
            return JsonError(HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal Server Error", result.Error().Message());
    }
    return JsonError(HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal Server Error", "Unknown bytecode explorer error");
}

bool CaseInsensitiveEquals(std::string_view left, std::string_view right)
{
    if (left.size() != right.size()) {
        return false;
    }
    return std::equal(left.begin(), left.end(), right.begin(), [](char leftValue, char rightValue) {
        const auto leftLowered = std::tolower(static_cast<unsigned char>(leftValue));
        const auto rightLowered = std::tolower(static_cast<unsigned char>(rightValue));
        return leftLowered == rightLowered;
    });
}

bool IsJsonContentType(std::string_view value)
{
    return CaseInsensitiveEquals(value, "application/json") ||
           CaseInsensitiveEquals(value, "application/json; charset=utf-8");
}

bool IsExactJsonContentType(std::string_view value)
{
    return value == "application/json";
}

std::string PickStatusJson(const PickController::State &state)
{
    using Json = nlohmann::json;
    Json path = state.name == "selected" && !state.path.empty() ? Json(state.path) : Json();
    Json error = state.name == "failed" && !state.error.empty() ? Json(state.error) : Json();
    return Json({{"state", state.name}, {"path", path}, {"error", error}}).dump();
}

std::optional<std::string> ParseOpenRequestBody(std::string_view body)
{
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.size() != 1U) {
        return std::nullopt;
    }
    const auto path = json.find("path");
    if (path == json.end() || !path->is_string()) {
        return std::nullopt;
    }
    return path->get<std::string>();
}

std::string PreferencesJson(const std::string &theme)
{
    return nlohmann::json({{"apiVersion", 1}, {"theme", theme}}).dump();
}

std::optional<std::string> ParseThemeRequestBody(std::string_view body)
{
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.size() != 1U) {
        return std::nullopt;
    }
    const auto theme = json.find("theme");
    if (theme == json.end() || !theme->is_string() ||
        !Preferences::IsKnownTheme(theme->get_ref<const std::string &>())) {
        return std::nullopt;
    }
    return theme->get<std::string>();
}

std::string RecentJson(const std::vector<RecentFileEntry> &entries)
{
    using Json = nlohmann::json;
    Json list = Json::array();
    for (const auto &entry : entries) {
        list.push_back({{"path", entry.path}, {"fileName", entry.fileName}, {"openedAt", entry.openedAtMs}});
    }
    return Json({{"apiVersion", 1}, {"entries", std::move(list)}}).dump();
}

using QueryParameters = std::vector<std::pair<std::string_view, std::string_view>>;

std::optional<QueryParameters> ParseExactQuery(std::string_view query,
                                               std::initializer_list<std::string_view> expectedKeys)
{
    if (query.empty()) {
        return std::nullopt;
    }

    QueryParameters parameters;
    size_t begin = 0U;
    while (begin <= query.size()) {
        const auto separator = query.find('&', begin);
        const auto pair =
            query.substr(begin, separator == std::string_view::npos ? query.size() - begin : separator - begin);
        const auto equals = pair.find('=');
        if (pair.empty() || equals == std::string_view::npos || equals == 0U || equals + 1U == pair.size() ||
            pair.find('=', equals + 1U) != std::string_view::npos) {
            return std::nullopt;
        }
        const auto key = pair.substr(0U, equals);
        const auto value = pair.substr(equals + 1U);
        if (std::find(expectedKeys.begin(), expectedKeys.end(), key) == expectedKeys.end() ||
            std::find_if(parameters.begin(), parameters.end(),
                         [key](const auto &parameter) { return parameter.first == key; }) != parameters.end()) {
            return std::nullopt;
        }
        parameters.emplace_back(key, value);
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1U;
    }

    if (parameters.size() != expectedKeys.size()) {
        return std::nullopt;
    }
    return parameters;
}

std::string_view QueryValue(const QueryParameters &parameters, std::string_view key)
{
    const auto parameter =
        std::find_if(parameters.begin(), parameters.end(), [key](const auto &item) { return item.first == key; });
    return parameter == parameters.end() ? std::string_view {} : parameter->second;
}

std::optional<uint64_t> ParseUnsigned(std::string_view value, int base)
{
    if (value.empty()) {
        return std::nullopt;
    }
    uint64_t parsed = 0U;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed, base);
    if (conversion.ec != std::errc {} || conversion.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<uint64_t> ParseHexOffset(std::string_view value)
{
    if (value.size() >= 2U && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        value.remove_prefix(2U);
    }
    return ParseUnsigned(value, HEXADECIMAL_RADIX);
}

using MetadataQueryParameters = std::vector<std::pair<std::string, std::string>>;

bool IsHexDigit(char value)
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

uint8_t HexDigitValue(char value)
{
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<uint8_t>(value - 'a' + HEXADECIMAL_LETTER_BASE);
    }
    return static_cast<uint8_t>(value - 'A' + HEXADECIMAL_LETTER_BASE);
}

// Length in bytes of the multi-byte UTF-8 sequence starting with the lead byte; 0 for invalid leads.
size_t Utf8SequenceLength(uint8_t first)
{
    if (first >= 0xC2U && first <= 0xDFU) {
        return 2U;
    }
    if (first >= 0xE0U && first <= 0xEFU) {
        return 3U;
    }
    if (first >= 0xF0U && first <= 0xF4U) {
        return 4U;
    }
    return 0U;
}

// Rejects overlong encodings and surrogates: the sequences whose second byte is out of range.
bool HasInvalidSecondByte(uint8_t first, uint8_t second)
{
    return (first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second > 0x9FU) ||
           (first == 0xF0U && second < 0x90U) || (first == 0xF4U && second > 0x8FU);
}

bool IsValidUtf8(std::string_view text)
{
    size_t index = 0U;
    while (index < text.size()) {
        const auto first = static_cast<uint8_t>(text[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        const auto length = Utf8SequenceLength(first);
        if (length == 0U) {
            return false;
        }
        if (length > text.size() - index) {
            return false;
        }
        for (size_t continuation = 1U; continuation < length; ++continuation) {
            if ((static_cast<uint8_t>(text[index + continuation]) & 0xC0U) != 0x80U) {
                return false;
            }
        }
        if (HasInvalidSecondByte(first, static_cast<uint8_t>(text[index + 1U]))) {
            return false;
        }
        index += length;
    }
    return true;
}

std::optional<std::string> DecodeMetadataQueryComponent(std::string_view encoded)
{
    std::string decoded;
    decoded.reserve(encoded.size());
    size_t index = 0U;
    while (index < encoded.size()) {
        const auto value = encoded[index];
        if (value == '+' || value == '\0') {
            return std::nullopt;
        }
        if (value != '%') {
            decoded.push_back(value);
            ++index;
            continue;
        }
        if (index + 2U >= encoded.size() || !IsHexDigit(encoded[index + 1U]) || !IsHexDigit(encoded[index + 2U])) {
            return std::nullopt;
        }
        const auto byte =
            static_cast<uint8_t>((HexDigitValue(encoded[index + 1U]) << 4U) | HexDigitValue(encoded[index + 2U]));
        if (byte == 0U) {
            return std::nullopt;
        }
        decoded.push_back(static_cast<char>(byte));
        index += 3U;
    }
    if (decoded.empty() || !IsValidUtf8(decoded)) {
        return std::nullopt;
    }
    return decoded;
}

bool ContainsKey(std::initializer_list<std::string_view> keys, std::string_view key)
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

std::optional<MetadataQueryParameters> ParseMetadataQuery(std::string_view query,
                                                          std::initializer_list<std::string_view> requiredKeys,
                                                          std::initializer_list<std::string_view> optionalKeys = {})
{
    if (query.empty()) {
        return std::nullopt;
    }
    MetadataQueryParameters parameters;
    size_t begin = 0U;
    while (begin <= query.size()) {
        const auto separator = query.find('&', begin);
        const auto pair =
            query.substr(begin, separator == std::string_view::npos ? query.size() - begin : separator - begin);
        const auto equals = pair.find('=');
        if (pair.empty() || equals == std::string_view::npos || equals == 0U || equals + 1U == pair.size()) {
            return std::nullopt;
        }
        auto key = DecodeMetadataQueryComponent(pair.substr(0U, equals));
        auto value = DecodeMetadataQueryComponent(pair.substr(equals + 1U));
        if (!key.has_value() || !value.has_value() ||
            (!ContainsKey(requiredKeys, *key) && !ContainsKey(optionalKeys, *key)) ||
            std::find_if(parameters.begin(), parameters.end(),
                         [&key](const auto &parameter) { return parameter.first == *key; }) != parameters.end()) {
            return std::nullopt;
        }
        parameters.emplace_back(std::move(*key), std::move(*value));
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1U;
    }
    for (const auto required : requiredKeys) {
        if (std::find_if(parameters.begin(), parameters.end(), [required](const auto &parameter) {
                return parameter.first == required;
            }) == parameters.end()) {
            return std::nullopt;
        }
    }
    return parameters;
}

std::string_view MetadataQueryValue(const MetadataQueryParameters &parameters, std::string_view key)
{
    const auto found = std::find_if(parameters.begin(), parameters.end(),
                                    [key](const auto &parameter) { return parameter.first == key; });
    return found == parameters.end() ? std::string_view {} : std::string_view(found->second);
}

bool ParseMetadataKind(std::string_view text, bool allowAll, std::optional<MetadataKind> *kind)
{
    if (allowAll && text == "All") {
        kind->reset();
        return true;
    }
    if (text == "String") {
        *kind = MetadataKind::STRING;
        return true;
    }
    if (text == "Method") {
        *kind = MetadataKind::METHOD;
        return true;
    }
    if (text == "Literal Array") {
        *kind = MetadataKind::LITERAL_ARRAY;
        return true;
    }
    return false;
}

std::optional<uint64_t> ParseCanonicalMetadataOffset(std::string_view value)
{
    if (value.size() <= 2U || value.substr(0U, 2U) != "0x" ||
        !std::all_of(value.begin() + 2U, value.end(), IsHexDigit)) {
        return std::nullopt;
    }
    return ParseUnsigned(value.substr(2U), HEXADECIMAL_RADIX);
}

std::optional<size_t> ParseMetadataLimit(std::string_view value)
{
    const auto parsed = ParseUnsigned(value, DECIMAL_RADIX);
    if (!parsed.has_value() || *parsed == 0U || *parsed > MAX_METADATA_PAGE_LIMIT) {
        return std::nullopt;
    }
    return static_cast<size_t>(*parsed);
}

// Statistics routes: one exact entity-id query selects a scope; GET reads the
// snapshot while POST starts or cancels the background scan. Nullopt means the
// path is neither statistics route.
std::optional<HttpResponse> RouteStatisticsRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.path != "/api/v1/statistics" && request.path != "/api/v1/statistics/cancel") {
        return std::nullopt;
    }
    const bool cancel = request.path == "/api/v1/statistics/cancel";
    if (request.method != "POST" && (cancel || request.method != "GET")) {
        return MethodNotAllowed(cancel ? "POST" : "GET, POST");
    }
    const auto parameters = ParseExactQuery(request.query, {"id"});
    if (!parameters.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Statistics requires exactly one entity id");
    }
    const auto idText = QueryValue(*parameters, "id");
    const auto id = ParseUnsigned(idText, DECIMAL_RADIX);
    if (!id.has_value() || *id == INVALID_ENTITY_ID || std::to_string(*id) != idText) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request",
                         "Statistics id must be a canonical positive decimal integer");
    }
    if (request.method == "GET") {
        return ApiResult(api.StatisticsJson(*id));
    }
    if (!IsExactJsonContentType(request.Header("content-type"))) {
        return JsonError(HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type",
                         "Statistics action requires application/json");
    }
    return ApiResult(cancel ? api.CancelStatisticsJson(*id, request.body) : api.StartStatisticsJson(*id, request.body));
}

// Metadata catalog routes; each handler keeps its original validation order.
HttpResponse RouteMetadataStatusRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.method != "GET") {
        return MethodNotAllowed("GET");
    }
    if (!request.query.empty()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Metadata status does not accept query parameters");
    }
    return ApiResult(api.MetadataStatusJson());
}

HttpResponse RouteMetadataIndexRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.method != "POST") {
        return MethodNotAllowed("POST");
    }
    if (!request.query.empty()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Metadata index does not accept query parameters");
    }
    if (!IsExactJsonContentType(request.Header("content-type"))) {
        return JsonError(HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type",
                         "Metadata index requests require application/json");
    }
    return ApiResult(api.StartMetadataIndexJson(request.body));
}

HttpResponse RouteMetadataObjectsRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.method != "GET") {
        return MethodNotAllowed("GET");
    }
    const auto parameters = ParseMetadataQuery(request.query, {"kind", "limit"}, {"query", "cursor"});
    if (!parameters.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Metadata objects query is invalid");
    }
    MetadataObjectQuery query;
    if (!ParseMetadataKind(MetadataQueryValue(*parameters, "kind"), true, &query.kind)) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Metadata kind is invalid");
    }
    const auto limit = ParseMetadataLimit(MetadataQueryValue(*parameters, "limit"));
    if (!limit.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Metadata limit must be between 1 and 200");
    }
    query.limit = *limit;
    const auto text = MetadataQueryValue(*parameters, "query");
    if (!text.empty()) {
        query.query = std::string(text);
    }
    const auto cursor = MetadataQueryValue(*parameters, "cursor");
    if (!cursor.empty()) {
        query.cursor = std::string(cursor);
    }
    return ApiResult(api.MetadataObjectsJson(query));
}

HttpResponse RouteMetadataObjectRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.method != "GET") {
        return MethodNotAllowed("GET");
    }
    const auto parameters = ParseMetadataQuery(request.query, {"kind", "offset"});
    if (!parameters.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Metadata object query is invalid");
    }
    std::optional<MetadataKind> kind;
    const auto offset = ParseCanonicalMetadataOffset(MetadataQueryValue(*parameters, "offset"));
    if (!ParseMetadataKind(MetadataQueryValue(*parameters, "kind"), false, &kind) || !offset.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Metadata object kind or offset is invalid");
    }
    return ApiResult(api.MetadataObjectJson({*kind, *offset}));
}

HttpResponse RouteMetadataReferencesRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.method != "GET") {
        return MethodNotAllowed("GET");
    }
    const auto parameters = ParseMetadataQuery(request.query, {"kind", "offset", "limit"}, {"cursor"});
    if (!parameters.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Metadata references query is invalid");
    }
    std::optional<MetadataKind> kind;
    const auto offset = ParseCanonicalMetadataOffset(MetadataQueryValue(*parameters, "offset"));
    const auto limit = ParseMetadataLimit(MetadataQueryValue(*parameters, "limit"));
    if (!ParseMetadataKind(MetadataQueryValue(*parameters, "kind"), false, &kind) || !offset.has_value() ||
        !limit.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request",
                         "Metadata references kind, offset or limit is invalid");
    }
    std::optional<std::string> cursor;
    const auto cursorText = MetadataQueryValue(*parameters, "cursor");
    if (!cursorText.empty()) {
        cursor = std::string(cursorText);
    }
    return ApiResult(api.MetadataReferencesJson({*kind, *offset}, std::move(cursor), *limit));
}

// Dispatches the metadata catalog routes to the dedicated handlers above, keeping
// the original route order; nullopt means the path is not a metadata route.
std::optional<HttpResponse> RouteMetadataRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.path == "/api/v1/metadata/status") {
        return RouteMetadataStatusRequest(request, api);
    }
    if (request.path == "/api/v1/metadata/index") {
        return RouteMetadataIndexRequest(request, api);
    }
    if (request.path == "/api/v1/metadata/objects") {
        return RouteMetadataObjectsRequest(request, api);
    }
    if (request.path == "/api/v1/metadata/object") {
        return RouteMetadataObjectRequest(request, api);
    }
    if (request.path == "/api/v1/metadata/references") {
        return RouteMetadataReferencesRequest(request, api);
    }
    return std::nullopt;
}

// Document summary and structure-tree routes; neither accepts a query (the
// guard in RouteApi has already rejected one). Nullopt means the path is neither.
std::optional<HttpResponse> RouteDocumentResourcesRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.path == "/api/v1/document") {
        if (request.method != "GET") {
            return MethodNotAllowed("GET");
        }
        return ApiResult(api.DocumentJson());
    }
    if (request.path == "/api/v1/structure") {
        if (request.method != "GET") {
            return MethodNotAllowed("GET");
        }
        return ApiResult(api.StructureJson());
    }
    return std::nullopt;
}

// Hex-window and location routes; the caller verified the path via
// acceptsOffsetQuery, so this always produces a response.
HttpResponse RouteOffsetResourcesRequest(const HttpRequest &request, const WebApi &api)
{
    if (request.path == "/api/v1/hex") {
        if (request.method != "GET") {
            return MethodNotAllowed("GET");
        }
        const auto parameters = ParseExactQuery(request.query, {"offset", "size"});
        if (!parameters.has_value()) {
            return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request",
                             "Hex requests require exactly one offset and one size");
        }
        const auto offset = ParseHexOffset(QueryValue(*parameters, "offset"));
        const auto size = ParseUnsigned(QueryValue(*parameters, "size"), DECIMAL_RADIX);
        if (!offset.has_value() || !size.has_value()) {
            return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request",
                             "Hex offset and size must be complete numeric values");
        }
        return ApiResult(api.HexJson(*offset, *size));
    }
    if (request.method != "GET") {
        return MethodNotAllowed("GET");
    }
    const auto parameters = ParseExactQuery(request.query, {"offset"});
    if (!parameters.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Location requests require exactly one offset");
    }
    const auto offset = ParseHexOffset(QueryValue(*parameters, "offset"));
    if (!offset.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request",
                         "Location offset must be a complete hexadecimal value");
    }
    return ApiResult(api.LocationJson(*offset));
}

// Entity-scoped analysis routes (disassembly, CFG, CFG layout) share the exact
// entity query; the path then selects the method check and response. The caller
// verified the path via acceptsEntityQuery, so this always produces a response.
HttpResponse RouteEntityResourcesRequest(const HttpRequest &request, const WebApi &api)
{
    auto parameters = ParseExactQuery(request.query, {"id"});
    bool hasMethodOffset = false;
    if (!parameters.has_value()) {
        parameters = ParseExactQuery(request.query, {"id", "methodOffset"});
        hasMethodOffset = parameters.has_value();
    }
    if (!parameters.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "This API route requires an exact entity query");
    }
    const auto id = ParseUnsigned(QueryValue(*parameters, "id"), DECIMAL_RADIX);
    if (!id.has_value() || *id == INVALID_ENTITY_ID) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Entity id must be a non-zero decimal integer");
    }
    std::optional<uint64_t> methodOffset;
    if (hasMethodOffset) {
        methodOffset = ParseHexOffset(QueryValue(*parameters, "methodOffset"));
        if (!methodOffset.has_value()) {
            return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request",
                             "Method offset must be a complete hexadecimal value");
        }
    }
    if (request.path == "/api/v1/disassembly") {
        if (request.method != "GET") {
            return MethodNotAllowed("GET");
        }
        return ApiResult(api.DisassemblyJson(*id, methodOffset));
    }
    if (request.path == "/api/v1/cfg") {
        if (request.method != "GET") {
            return MethodNotAllowed("GET");
        }
        return ApiResult(api.CfgJson(*id, methodOffset));
    }
    if (request.method != "POST") {
        return MethodNotAllowed("POST");
    }
    if (!IsJsonContentType(request.Header("content-type"))) {
        return JsonError(HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type",
                         "CFG layout requests require application/json");
    }
    return ApiResult(api.CfgLayoutJson(*id, request.body, methodOffset));
}

}  // namespace

std::string_view HttpResponse::Header(std::string_view name) const
{
    const auto found = std::find_if(headers.begin(), headers.end(),
                                    [name](const auto &header) { return CaseInsensitiveEquals(header.first, name); });
    return found == headers.end() ? std::string_view {} : std::string_view(found->second);
}

std::string HttpResponse::Serialize() const
{
    std::ostringstream output;
    output << "HTTP/1.1 " << status << ' ' << reason << "\r\n";
    output << "Content-Type: " << contentType << "\r\n";
    output << "Content-Length: " << body.size() << "\r\n";
    output << "Connection: close\r\n";
    for (const auto &[name, value] : headers) {
        output << name << ": " << value << "\r\n";
    }
    output << "\r\n" << body;
    return output.str();
}

RequestRouter::RequestRouter(const WebSession &session, std::string token, PickController &pick, RecentFiles &recents,
                             Preferences &preferences, uint16_t port)
    : session_(session), pick_(pick), recents_(recents), preferences_(preferences), token_(std::move(token)),
      numericLoopbackHost_("127.0.0.1:" + std::to_string(port)), namedLoopbackHost_("localhost:" + std::to_string(port))
{
}

HttpResponse RequestRouter::Route(const HttpRequest &request) const
{
    if (request.path.rfind("/api/", 0U) == 0U) {
        if (!IsAuthorized(request)) {
            auto response =
                JsonError(HTTP_STATUS_UNAUTHORIZED, "Unauthorized", "A valid local session bearer token is required");
            response.headers.emplace_back("WWW-Authenticate", "Bearer");
            return response;
        }
        if (request.path == "/api/v1/session/status") {
            if (request.method != "GET") {
                return MethodNotAllowed("GET");
            }
            if (!request.query.empty()) {
                return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Session status accepts no query");
            }
            return Response(HTTP_STATUS_OK, "OK", "application/json; charset=utf-8", session_.StatusJson());
        }
        if (request.path == "/api/v1/recent") {
            if (request.method != "GET") {
                return MethodNotAllowed("GET");
            }
            if (!request.query.empty()) {
                return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Recent accepts no query");
            }
            return Response(HTTP_STATUS_OK, "OK", "application/json; charset=utf-8", RecentJson(recents_.List()));
        }
        if (request.path == "/api/v1/preferences") {
            return RoutePreferences(request);
        }
        if (request.path == "/api/v1/pick" || request.path == "/api/v1/pick/status" || request.path == "/api/v1/open") {
            return RouteDocumentSwitch(request);
        }
        return RouteSessionApi(request);
    }

    const auto asset = FindEmbeddedWebAsset(request.path);
    if (!asset.has_value()) {
        return JsonError(HTTP_STATUS_NOT_FOUND, "Not Found", "Unknown local bytecode explorer route");
    }
    if (request.method != "GET") {
        return MethodNotAllowed("GET");
    }
    return Response(HTTP_STATUS_OK, "OK", std::string(asset->mimeType), std::string(asset->content));
}

bool RequestRouter::IsLoopbackHost(const HttpRequest &request) const
{
    const auto host = request.Header("host");
    return CaseInsensitiveEquals(host, numericLoopbackHost_) || CaseInsensitiveEquals(host, namedLoopbackHost_);
}

HttpResponse RequestRouter::RoutePreferences(const HttpRequest &request) const
{
    if (!request.query.empty()) return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Preferences accept no query");
    if (request.method == "GET") {
        return Response(HTTP_STATUS_OK, "OK", "application/json; charset=utf-8", PreferencesJson(preferences_.Theme()));
    }
    if (request.method != "PUT") return MethodNotAllowed("GET, PUT");
    // The write side changes a local file: require a loopback Host header
    // like the document-switch routes do.
    if (!IsLoopbackHost(request)) {
        return JsonError(HTTP_STATUS_FORBIDDEN, "Forbidden", "Cross-origin requests cannot change local preferences");
    }
    if (!IsExactJsonContentType(request.Header("content-type"))) {
        return JsonError(HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type",
                         "Preferences require application/json");
    }
    const auto theme = ParseThemeRequestBody(request.body);
    if (!theme.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request",
                         "Preferences require a JSON object with exactly one known theme string");
    }
    if (!preferences_.SetTheme(*theme)) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Unknown theme");
    }
    return Response(HTTP_STATUS_OK, "OK", "application/json; charset=utf-8", PreferencesJson(preferences_.Theme()));
}

HttpResponse RequestRouter::RouteDocumentSwitch(const HttpRequest &request) const
{
    if (!IsLoopbackHost(request)) {
        return JsonError(HTTP_STATUS_FORBIDDEN, "Forbidden", "Cross-origin requests cannot switch the local document");
    }
    if (request.path == "/api/v1/pick") {
        if (request.method != "POST") return MethodNotAllowed("POST");
        if (!request.query.empty()) {
            return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Pick accepts no query");
        }
        if (!request.body.empty()) {
            return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Pick accepts no request body");
        }
        if (!pick_.StartPick()) {
            return JsonError(HTTP_STATUS_CONFLICT, "Conflict", "A file dialog is already open");
        }
        return Response(HTTP_STATUS_ACCEPTED, "Accepted", "application/json; charset=utf-8", R"({"state":"picking"})");
    }
    if (request.path == "/api/v1/pick/status") {
        if (request.method != "GET") return MethodNotAllowed("GET");
        if (!request.query.empty()) {
            return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Pick status accepts no query");
        }
        return Response(HTTP_STATUS_OK, "OK", "application/json; charset=utf-8", PickStatusJson(pick_.QueryState()));
    }
    if (request.method != "POST") return MethodNotAllowed("POST");
    if (!request.query.empty()) return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Open accepts no query");
    if (!IsExactJsonContentType(request.Header("content-type"))) {
        return JsonError(HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type",
                         "Open requires application/json");
    }
    const auto path = ParseOpenRequestBody(request.body);
    if (!path.has_value()) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request",
                         "Open requires a JSON object with exactly one path string");
    }
    switch (pick_.OpenPath(*path)) {
        case PickController::OpenStatus::ACCEPTED:
            // Recent-file tracking happens in the session's document-opened
            // callback: only loads that actually publish a document count.
            return Response(HTTP_STATUS_ACCEPTED, "Accepted", "application/json; charset=utf-8",
                            R"({"state":"switching"})");
        case PickController::OpenStatus::BUSY:
            return JsonError(HTTP_STATUS_CONFLICT, "Conflict", "A document load is already in progress");
        case PickController::OpenStatus::INVALID:
            break;
    }
    return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "Open requires a non-empty path without NUL bytes");
}

bool RequestRouter::IsAuthorized(const HttpRequest &request) const
{
    const auto authorization = request.Header("authorization");
    if (authorization.size() <= BEARER_PREFIX.size() ||
        authorization.substr(0U, BEARER_PREFIX.size()) != BEARER_PREFIX) {
        return false;
    }
    return SecureTokenEquals(authorization.substr(BEARER_PREFIX.size()), token_);
}

// Document-backed API routes: every session state that is not READY reports its
// own diagnostic; only a READY session reaches RouteApi.
HttpResponse RequestRouter::RouteSessionApi(const HttpRequest &request) const
{
    const auto status = session_.Status();
    if (status.state != WebSessionState::READY) {
        if (status.state == WebSessionState::FAILED) {
            return ApiResult(Result<std::string>::Failure(
                Status(status.errorCode, status.error.empty() ? "Cannot load ABC" : status.error)));
        }
        if (status.state == WebSessionState::CANCELLED) {
            return JsonError(HTTP_STATUS_CONFLICT, "Conflict", "ABC loading was cancelled");
        }
        if (status.state == WebSessionState::EMPTY) {
            return JsonError(HTTP_STATUS_SERVICE_UNAVAILABLE, "Service Unavailable", "No ABC file is open yet");
        }
        auto response = JsonError(HTTP_STATUS_SERVICE_UNAVAILABLE, "Service Unavailable",
                                  "ABC is still loading; check session status");
        response.headers.emplace_back("Retry-After", "1");
        return response;
    }
    return RouteApi(request, *session_.Api());
}

HttpResponse RequestRouter::RouteApi(const HttpRequest &request, const WebApi &api) const
{
    if (auto response = RouteStatisticsRequest(request, api)) {
        return std::move(*response);
    }
    if (auto response = RouteMetadataRequest(request, api)) {
        return std::move(*response);
    }

    const bool acceptsEntityQuery =
        request.path == "/api/v1/disassembly" || request.path == "/api/v1/cfg" || request.path == "/api/v1/cfg/layout";
    const bool acceptsOffsetQuery = request.path == "/api/v1/hex" || request.path == "/api/v1/location";
    if (!request.query.empty() && !acceptsEntityQuery && !acceptsOffsetQuery) {
        return JsonError(HTTP_STATUS_BAD_REQUEST, "Bad Request", "This API route does not accept query parameters");
    }
    if (auto document = RouteDocumentResourcesRequest(request, api)) {
        return std::move(*document);
    }
    if (acceptsOffsetQuery) {
        return RouteOffsetResourcesRequest(request, api);
    }
    if (acceptsEntityQuery) {
        return RouteEntityResourcesRequest(request, api);
    }
    return JsonError(HTTP_STATUS_NOT_FOUND, "Not Found", "Unknown bytecode explorer API route");
}

}  // namespace panda::bytecode_explorer
