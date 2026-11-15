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
#include "web/host/loopback_host.h"
#include "web/host/pick_controller.h"
#include "web/host/request_router.h"

#include "app/document_loader.h"
#include "core/executable_target_resolver.h"
#include "platform/browser_launcher.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace panda::bytecode_explorer::test {
namespace {

// HTTP status codes asserted by the route tests in this file.
constexpr int HTTP_STATUS_OK = 200;
constexpr int HTTP_STATUS_BAD_REQUEST = 400;
constexpr int HTTP_STATUS_UNAUTHORIZED = 401;
constexpr int HTTP_STATUS_METHOD_NOT_ALLOWED = 405;
constexpr int HTTP_STATUS_CONFLICT = 409;
constexpr int HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415;

std::string MetadataHex(uint64_t value)
{
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << value;
    return output.str();
}

TEST(WebHttpTest, ParsesOneBoundedGetRequest)
{
    const std::string bytes =
        "GET /api/disassembly?id=42 HTTP/1.1\r\nHost: 127.0.0.1:1234\r\nAuthorization: Bearer abc123\r\n\r\n";

    const auto parsed = ParseHttpRequest(bytes);

    ASSERT_EQ(parsed.status, HttpParseStatus::COMPLETE) << parsed.diagnostic;
    EXPECT_EQ(parsed.request.method, "GET");
    EXPECT_EQ(parsed.request.target, "/api/disassembly?id=42");
    EXPECT_EQ(parsed.request.path, "/api/disassembly");
    EXPECT_EQ(parsed.request.query, "id=42");
    EXPECT_EQ(parsed.request.Header("authorization"), "Bearer abc123");
}

TEST(WebHttpTest, WaitsForTheHeaderTerminator)
{
    const auto parsed = ParseHttpRequest("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n");

    EXPECT_EQ(parsed.status, HttpParseStatus::INCOMPLETE);
}

TEST(WebHttpTest, ParsesOneBoundedJsonPostAfterTheCompleteBodyArrives)
{
    const std::string header =
        "POST /api/v1/cfg/layout?id=42 HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
        "Content-Length: 14\r\n\r\n";

    EXPECT_EQ(ParseHttpRequest(header + R"({"revision":)").status, HttpParseStatus::INCOMPLETE);
    const auto parsed = ParseHttpRequest(header + R"({"revision":1})");

    ASSERT_EQ(parsed.status, HttpParseStatus::COMPLETE) << parsed.diagnostic;
    EXPECT_EQ(parsed.request.method, "POST");
    EXPECT_EQ(parsed.request.path, "/api/v1/cfg/layout");
    EXPECT_EQ(parsed.request.body, R"({"revision":1})");
}

TEST(WebHttpTest, RejectsBodiesDuplicateHeadersAndOversizedInput)
{
    EXPECT_EQ(HttpRequestLimits::MAX_BODY_BYTES, 1024U * 1024U);
    EXPECT_EQ(ParseHttpRequest("GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\nx").status,
              HttpParseStatus::INVALID);
    EXPECT_EQ(ParseHttpRequest("GET / HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n").status, HttpParseStatus::INVALID);
    EXPECT_EQ(ParseHttpRequest(std::string(HttpRequestLimits::MAX_HEADER_BYTES + 1U, 'x')).status,
              HttpParseStatus::TOO_LARGE);
    const std::string oversizedBodyHeader =
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(HttpRequestLimits::MAX_BODY_BYTES + 1U) +
        "\r\n\r\n";
    EXPECT_EQ(ParseHttpRequest(oversizedBodyHeader).status, HttpParseStatus::TOO_LARGE);
    const std::string boundaryHeader =
        "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(HttpRequestLimits::MAX_BODY_BYTES) +
        "\r\n\r\n";
    const auto boundary = ParseHttpRequest(boundaryHeader + std::string(HttpRequestLimits::MAX_BODY_BYTES, 'x'));
    EXPECT_EQ(boundary.status, HttpParseStatus::COMPLETE) << boundary.diagnostic;
    EXPECT_EQ(boundary.request.body.size(), HttpRequestLimits::MAX_BODY_BYTES);
}

TEST(WebHttpTest, EnforcesTheExactRequestTargetBoundaryAndRejectsFragments)
{
    const auto requestBytes = [](const std::string &target) {
        return "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    };
    const std::string boundaryTarget = "/" + std::string(HttpRequestLimits::MAX_TARGET_BYTES - 1U, 'a');

    const auto boundary = ParseHttpRequest(requestBytes(boundaryTarget));
    EXPECT_EQ(boundary.status, HttpParseStatus::COMPLETE) << boundary.diagnostic;
    EXPECT_EQ(ParseHttpRequest(requestBytes(boundaryTarget + "a")).status, HttpParseStatus::INVALID);
    EXPECT_EQ(ParseHttpRequest(requestBytes("/api/v1/metadata/status#fragment")).status, HttpParseStatus::INVALID);
}

TEST(WebHttpTest, ComparesBearerTokensWithoutAnEarlyExit)
{
    EXPECT_TRUE(SecureTokenEquals("0123456789abcdef", "0123456789abcdef"));
    EXPECT_FALSE(SecureTokenEquals("0123456789abcdef", "0123456789abcdee"));
    EXPECT_FALSE(SecureTokenEquals("short", "0123456789abcdef"));
}

TEST(WebHttpTest, ReportsAuthenticatedSessionReadinessBeforeDocumentRequests)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 's');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/session/status";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED);
    request.headers["authorization"] = "Bearer " + token;
    const auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_OK) << response.body;
    const auto status = nlohmann::json::parse(response.body);
    EXPECT_EQ(status.at("state"), "ready");
    EXPECT_EQ(status.at("stage"), "ready");
    EXPECT_TRUE(status.at("error").is_null());
    request.query = "unexpected=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query.clear();
    request.method = "POST";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_METHOD_NOT_ALLOWED);
}

TEST(WebHttpTest, StatisticsRoutesAuthenticateAndValidateScopeActions)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue());
    const auto rootId = loaded.Value().structure->RootIds().front();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 't');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/statistics";
    request.query = "id=" + std::to_string(rootId);
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED);
    request.headers["authorization"] = "Bearer " + token;
    auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_OK) << response.body;
    EXPECT_EQ(nlohmann::json::parse(response.body).at("phase"), "NotStarted");
    for (const auto *query : {"", "id=0", "id=-1", "id=01", "id=1&id=2", "id=1&extra=1", "id=18446744073709551616"}) {
        request.query = query;
        EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST) << query;
    }
    request.query = "id=" + std::to_string(rootId);
    request.method = "PUT";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_METHOD_NOT_ALLOWED);
    request.method = "POST";
    request.body = "{}";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
    request.headers["content-type"] = "application/json";
    for (const auto *body : {"", "null", "[]", "{", "{\"unknown\":1}"}) {
        request.body = body;
        EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST) << body;
    }
    request.body = "{}";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    request.path = "/api/v1/statistics/cancel";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    request.method = "GET";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_METHOD_NOT_ALLOWED);
    request.headers.erase("authorization");
    request.method = "POST";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED);
}

void ExpectIndexServed(RequestRouter &router)
{
    HttpRequest request;
    request.method = "GET";
    request.target = "/";
    request.path = "/";
    const auto asset = router.Route(request);
    EXPECT_EQ(asset.status, HTTP_STATUS_OK);
    EXPECT_EQ(asset.contentType, "text/html; charset=utf-8");
    EXPECT_EQ(asset.body.find("structure_sample.abc"), std::string::npos);
    EXPECT_NE(asset.body.find("Ark Bytecode Explorer"), std::string::npos);
    EXPECT_NE(asset.body.find("id=\"workbench\""), std::string::npos);
    EXPECT_NE(asset.body.find("id=\"disassembly-panel\""), std::string::npos);
    EXPECT_NE(asset.body.find("id=\"cfg-panel\""), std::string::npos);
    EXPECT_NE(asset.body.find("id=\"hex-panel\""), std::string::npos);
    EXPECT_NE(asset.body.find("id=\"hex-view\""), std::string::npos);
    EXPECT_NE(asset.body.find("id=\"metadata-panel\""), std::string::npos);
    EXPECT_NE(asset.body.find("id=\"metadata-view\""), std::string::npos);
    EXPECT_EQ(asset.body.find("class=\"view-tabs\""), std::string::npos);
}

void ExpectAssetServed(RequestRouter &router, HttpRequest &request, const char *path, const char *marker,
                       const char *contentType = nullptr)
{
    request.method = "GET";
    request.target = path;
    request.path = path;
    const auto asset = router.Route(request);
    EXPECT_EQ(asset.status, HTTP_STATUS_OK);
    if (contentType != nullptr) {
        EXPECT_EQ(asset.contentType, contentType);
    }
    EXPECT_NE(asset.body.find(marker), std::string::npos);
}

TEST(WebHttpTest, RouterKeepsStaticShellDataFreeAndAuthenticatesEveryApi)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'a');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);

    ExpectIndexServed(router);

    HttpRequest assetRequest;

    ExpectAssetServed(router, assetRequest, "/assets/cfg-view.js", "export class CfgView",
                      "text/javascript; charset=utf-8");
    ExpectAssetServed(router, assetRequest, "/assets/cfg-view-model.js", "export function buildLayoutRequest");
    ExpectAssetServed(router, assetRequest, "/assets/cfg-canvas.js", "export class CfgCanvas");
    ExpectAssetServed(router, assetRequest, "/assets/hex-view-model.js", "export function parseOffsetText");
    ExpectAssetServed(router, assetRequest, "/assets/hex-view.js", "export class HexView");
    ExpectAssetServed(router, assetRequest, "/assets/metadata-view-model.js", "export function metadataKey");
    ExpectAssetServed(router, assetRequest, "/assets/metadata-view.js", "export class MetadataView");
    ExpectAssetServed(router, assetRequest, "/assets/workbench-layout.js", "export class WorkbenchLayout");
    ExpectAssetServed(router, assetRequest, "/assets/workbench-selection.js",
                      "export async function loadExecutableWorkbenchViews");

    HttpRequest apiRequest;
    apiRequest.method = "GET";
    apiRequest.target = "/api/v1/document";
    apiRequest.path = "/api/v1/document";
    EXPECT_EQ(router.Route(apiRequest).status, HTTP_STATUS_UNAUTHORIZED);
    apiRequest.headers["authorization"] = "Bearer wrong";
    EXPECT_EQ(router.Route(apiRequest).status, HTTP_STATUS_UNAUTHORIZED);
    apiRequest.headers["authorization"] = "Bearer " + token;
    const auto response = router.Route(apiRequest);
    EXPECT_EQ(response.status, HTTP_STATUS_OK);
    EXPECT_EQ(response.contentType, "application/json; charset=utf-8");
    EXPECT_EQ(response.Header("Content-Security-Policy").find("default-src 'self'"), 0U);
    EXPECT_EQ(response.Header("Cache-Control"), "no-store");
}

TEST(WebHttpTest, RouterAcceptsAnExactMethodOffsetForDisassembly)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto nodes = loaded.Value().structure->NodesInIdOrder();
    const auto executable = std::find_if(nodes.begin(), nodes.end(), [](const EntityNode *node) {
        return !ExecutableTargetResolver::Resolve(*node).empty();
    });
    ASSERT_NE(executable, nodes.end());
    const auto target = ExecutableTargetResolver::Resolve(**executable).front();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'e');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/disassembly";
    request.query = "id=" + std::to_string((*executable)->id) + "&methodOffset=" + MetadataHex(target.methodOffset);
    request.headers["authorization"] = "Bearer " + token;

    const auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_OK) << response.body;
    EXPECT_EQ(nlohmann::json::parse(response.body).at("function").at("methodOffset"), MetadataHex(target.methodOffset));

    request.query += "&extra=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "id=" + std::to_string((*executable)->id) + "&methodOffset=0xGG";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
}

TEST(WebHttpTest, RouterKeepsExactMethodOffsetForCfgAndRelayout)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto nodes = loaded.Value().structure->NodesInIdOrder();
    const auto executable = std::find_if(nodes.begin(), nodes.end(), [](const EntityNode *node) {
        return !ExecutableTargetResolver::Resolve(*node).empty();
    });
    ASSERT_NE(executable, nodes.end());
    const auto target = ExecutableTargetResolver::Resolve(**executable).front();
    const auto entityId = std::to_string((*executable)->id);
    const auto methodOffset = MetadataHex(target.methodOffset);
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'f');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/cfg";
    request.query = "id=" + entityId + "&methodOffset=" + methodOffset;
    request.headers["authorization"] = "Bearer " + token;

    auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_OK) << response.body;
    EXPECT_EQ(nlohmann::json::parse(response.body).at("function").at("methodOffset"), methodOffset);

    request.method = "POST";
    request.path = "/api/v1/cfg/layout";
    request.headers["content-type"] = "application/json";
    request.body = R"({"revision":7,"nodes":[]})";
    response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_OK) << response.body;
    const auto relayout = nlohmann::json::parse(response.body);
    EXPECT_EQ(relayout.at("function").at("methodOffset"), methodOffset);
    EXPECT_EQ(relayout.at("layout").at("revision"), 7U);

    request.query += "&extra=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "id=" + entityId + "&methodOffset=0xGG";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
}

TEST(WebHttpTest, RouterAcceptsOnlyAuthenticatedJsonPostForCfgRelayout)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto nodes = loaded.Value().structure->NodesInIdOrder();
    const auto executable = std::find_if(nodes.begin(), nodes.end(), [](const EntityNode *node) {
        return !ExecutableTargetResolver::Resolve(*node).empty();
    });
    ASSERT_NE(executable, nodes.end());
    const auto entityId = (*executable)->id;
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'b');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "POST";
    request.target = "/api/v1/cfg/layout?id=" + std::to_string(entityId);
    request.path = "/api/v1/cfg/layout";
    request.query = "id=" + std::to_string(entityId);
    request.body = R"({"revision":2,"nodes":[]})";

    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED);
    request.headers["authorization"] = "Bearer " + token;
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
    request.headers["content-type"] = "application/json";
    const auto response = router.Route(request);
    EXPECT_EQ(response.status, HTTP_STATUS_OK) << response.body;
    const auto json = nlohmann::json::parse(response.body);
    EXPECT_EQ(json.at("layout").at("revision"), 2U);

    request.method = "GET";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_METHOD_NOT_ALLOWED);
}

TEST(WebHttpTest, RouterRequiresExactAuthenticatedHexQuery)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'c');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/hex";
    request.query = "offset=0x0&size=16";

    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED);
    request.headers["authorization"] = "Bearer " + token;
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);

    request.query = "offset=0x0";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0xGG&size=16";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&size=65537";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&size=16&extra=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&offset=0x10&size=16";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0junk&size=16";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&&size=16";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=&size=16";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&size=";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0=1&size=16";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&size=16=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x10000000000000000&size=16";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&size=18446744073709551616";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);

    request.query = "offset=0x0&size=16";
    request.method = "POST";
    const auto method = router.Route(request);
    EXPECT_EQ(method.status, HTTP_STATUS_METHOD_NOT_ALLOWED);
    EXPECT_EQ(method.Header("Allow"), "GET");
}

TEST(WebHttpTest, RouterRequiresExactAuthenticatedLocationQuery)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'd');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/location";
    request.query = "offset=0x0";

    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED);
    request.headers["authorization"] = "Bearer " + token;
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);

    request.query.clear();
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0xGG";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&extra=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&offset=0x10";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0junk";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0&";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x0=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "offset=0x10000000000000000";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);

    request.query = "offset=0x0";
    request.method = "POST";
    const auto method = router.Route(request);
    EXPECT_EQ(method.status, HTTP_STATUS_METHOD_NOT_ALLOWED);
    EXPECT_EQ(method.Header("Allow"), "GET");
}

void FindFirstReferencedString(LoadedDocumentResult &loaded, std::string &offset)
{
    const auto object =
        std::find_if(loaded.Value().metadata->Objects().begin(), loaded.Value().metadata->Objects().end(),
                     [](const MetadataObject &candidate) {
                         return candidate.key.kind == MetadataKind::STRING && candidate.referenceCount > 0U;
                     });
    ASSERT_NE(object, loaded.Value().metadata->Objects().end());
    offset = MetadataHex(object->key.offset);
}

struct RouteCase {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::string contentType;
    std::string allow;
};

void ExpectMetadataRouteAuthorized(RequestRouter &router, const std::string &token, const RouteCase &route,
                                   std::string &documentId)
{
    HttpRequest request;
    request.method = route.method;
    request.target = route.path + (route.query.empty() ? "" : "?" + route.query);
    request.path = route.path;
    request.query = route.query;
    request.body = route.body;
    if (!route.contentType.empty()) {
        request.headers["content-type"] = route.contentType;
    }
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED) << route.path;
    request.headers["authorization"] = "Bearer wrong";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED) << route.path;
    request.headers["authorization"] = "Bearer " + token;

    const auto originalMethod = request.method;
    request.method = originalMethod == "GET" ? "POST" : "GET";
    const auto wrongMethod = router.Route(request);
    EXPECT_EQ(wrongMethod.status, HTTP_STATUS_METHOD_NOT_ALLOWED) << route.path;
    EXPECT_EQ(wrongMethod.Header("Allow"), route.allow) << route.path;
    request.method = originalMethod;

    const auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_OK) << route.path << ": " << response.body;
    const auto json = nlohmann::json::parse(response.body);
    EXPECT_EQ(json.at("apiVersion"), 1) << route.path;
    if (documentId.empty()) {
        documentId = json.at("documentId").get<std::string>();
    }
    EXPECT_EQ(json.at("documentId"), documentId) << route.path;
}

TEST(WebHttpTest, RouterAuthenticatesAndRestrictsEveryMetadataRoute)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    std::string offset;
    ASSERT_NO_FATAL_FAILURE(FindFirstReferencedString(loaded, offset));
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'm');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);

    const std::vector<RouteCase> routes {
        {"GET", "/api/v1/metadata/status", "", "", "", "GET"},
        {"POST", "/api/v1/metadata/index", "", "{}", "application/json", "POST"},
        {"GET", "/api/v1/metadata/objects", "kind=All&limit=100", "", "", "GET"},
        {"GET", "/api/v1/metadata/object", "kind=String&offset=" + offset, "", "", "GET"},
        {"GET", "/api/v1/metadata/references", "kind=String&offset=" + offset + "&limit=100", "", "", "GET"},
    };

    std::string documentId;
    for (const auto &route : routes) {
        ASSERT_NO_FATAL_FAILURE(ExpectMetadataRouteAuthorized(router, token, route, documentId));
    }
    session.Api()->WaitForMetadataIndexForTesting();
}

TEST(WebHttpTest, RouterRequiresExactJsonAndEmptyObjectForMetadataIndex)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'n');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "POST";
    request.path = "/api/v1/metadata/index";
    request.target = request.path;
    request.body = "{}";
    request.headers["authorization"] = "Bearer " + token;

    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
    request.headers["content-type"] = "application/json; charset=utf-8";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
    request.headers["content-type"] = "Application/Json";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
    request.headers["content-type"] = "application/json";

    for (const std::string body : {"", "[]", R"({"x":1})", "{}{}", "{} trailing"}) {
        request.body = body;
        EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST) << body;
    }
    request.body = "{}";
    request.query = "extra=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query.clear();
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    session.Api()->WaitForMetadataIndexForTesting();
}

void ExpectObjectQueriesRejected(RequestRouter &router, HttpRequest &request)
{
    const std::vector<std::string> queries {
        "",
        "kind=&limit=100",
        "kind=All&limit=",
        "kind=All&&limit=100",
        "kind=All&limit=100&",
        "kind=All&limit=100&extra=1",
        "kind=All&kind=String&limit=100",
        "kind=All&limit=100&limit=1",
        "kind=%GG&limit=100",
        "kind=All&query=a+b&limit=100",
        "kind=All&query=%00&limit=100",
        "kind=All&query=%C3%28&limit=100",
        "kind=Unknown&limit=100",
        "kind=all&limit=100",
        "kind=All&limit=0",
        "kind=All&limit=201",
        "kind=All&limit=18446744073709551616",
        "kind=All&limit=100&cursor=bad",
    };
    for (const auto &query : queries) {
        request.query = query;
        EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST) << query;
    }
}

void ExpectObjectKeyQueriesRejected(RequestRouter &router, HttpRequest &request)
{
    const std::vector<std::string> keys {
        "kind=String&offset=",
        "kind=String&offset=300",
        "kind=String&offset=0X300",
        "kind=String&offset=0x",
        "kind=String&offset=+0x300",
        "kind=String&offset=0x300junk",
        "kind=String&offset=0x10000000000000000",
        "kind=String&offset=0x300&offset=0x300",
        "kind=String&offset=0x300&unknown=1",
    };
    for (const auto &query : keys) {
        request.query = query;
        EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST) << query;
    }
}

void ExpectReferenceQueriesBound(RequestRouter &router, HttpRequest &request, const std::string &offset)
{
    request.path = "/api/v1/metadata/references";
    request.query = "kind=String&offset=" + offset + "&limit=100";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    request.query = "kind=String&offset=" + offset + "&limit=100&cursor=malformed";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "kind=String&offset=" + offset + "&limit=0";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "kind=String&offset=" + offset + "&limit=201";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
}

void ExpectStaleCursorRejected(RequestRouter &router, WebSession &session, HttpRequest &request,
                               const std::string &token, const nlohmann::json &cursor)
{
    HttpRequest start;
    start.method = "POST";
    start.path = "/api/v1/metadata/index";
    start.body = "{}";
    start.headers["authorization"] = "Bearer " + token;
    start.headers["content-type"] = "application/json";
    ASSERT_EQ(router.Route(start).status, HTTP_STATUS_OK);
    session.Api()->WaitForMetadataIndexForTesting();
    request.query = "kind=String&limit=1&cursor=" + cursor.get<std::string>();
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_CONFLICT);
}

TEST(WebHttpTest, RouterStrictlyParsesBoundedMetadataQueriesAndMapsStaleCursor)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    std::string offset;
    ASSERT_NO_FATAL_FAILURE(FindFirstReferencedString(loaded, offset));
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'q');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/metadata/objects";
    request.headers["authorization"] = "Bearer " + token;
    request.query = "kind=All&limit=100";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    request.query = "kind=Literal%20Array&limit=100";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    request.query = "kind=String&query=%42eta&limit=100";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    request.query = "kind=All&query=" + std::string(256U, 'x') + "&limit=100";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);

    ExpectObjectQueriesRejected(router, request);
    request.query = std::string("kind=All&query=a") + '\0' + "b&limit=100";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "kind=All&query=" + std::string(257U, 'x') + "&limit=100";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);

    request.path = "/api/v1/metadata/object";
    request.query = "kind=String&offset=" + offset;
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    ExpectObjectKeyQueriesRejected(router, request);

    ExpectReferenceQueriesBound(router, request, offset);

    request.path = "/api/v1/metadata/status";
    request.query = "empty=";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.query = "unknown=1";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);

    request.path = "/api/v1/metadata/objects";
    request.query = "kind=String&limit=1";
    const auto firstPage = router.Route(request);
    ASSERT_EQ(firstPage.status, HTTP_STATUS_OK) << firstPage.body;
    const auto cursor = nlohmann::json::parse(firstPage.body).at("nextCursor");
    ASSERT_TRUE(cursor.is_string());

    ASSERT_NO_FATAL_FAILURE(ExpectStaleCursorRejected(router, session, request, token, cursor));
}

TEST(WebHttpTest, RealHttpMetadataQueriesRejectEveryInvalidUtf8Class)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'u');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    const std::vector<std::pair<std::string, std::string>> cases {
        {"overlong", "%C0%AF"},
        {"surrogate", "%ED%A0%80"},
        {"out-of-range", "%F4%90%80%80"},
        {"truncated", "%E2%82"},
    };

    for (const auto &[name, encoded] : cases) {
        const auto target = "/api/v1/metadata/objects?kind=All&query=" + encoded + "&limit=100";
        const auto bytes =
            "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token + "\r\n\r\n";
        const auto parsed = ParseHttpRequest(bytes);
        ASSERT_EQ(parsed.status, HttpParseStatus::COMPLETE) << name << ": " << parsed.diagnostic;
        EXPECT_EQ(router.Route(parsed.request).status, HTTP_STATUS_BAD_REQUEST) << name;
    }
}

TEST(WebHttpTest, MetadataAuthenticationPrecedesAllMalformedRouteInputs)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebSession session(std::move(loaded.Value()));
    const std::string token(64U, 'p');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    const std::vector<HttpRequest> malformed {
        {"POST", "/api/v1/metadata/status?bad=%GG", "/api/v1/metadata/status", "bad=%GG", "trailing", {}},
        {"GET",
         "/api/v1/metadata/index?bad=%GG",
         "/api/v1/metadata/index",
         "bad=%GG",
         "[]",
         {{"content-type", "text/plain"}}},
        {"POST", "/api/v1/metadata/objects?bad=%GG", "/api/v1/metadata/objects", "bad=%GG", "trailing", {}},
        {"POST", "/api/v1/metadata/object?bad=%GG", "/api/v1/metadata/object", "bad=%GG", "trailing", {}},
        {"POST", "/api/v1/metadata/references?bad=%GG", "/api/v1/metadata/references", "bad=%GG", "trailing", {}},
    };

    for (auto request : malformed) {
        EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED) << request.path;
        request.headers["authorization"] = "Bearer wrong";
        EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED) << request.path;
    }
}

TEST(WebHttpTest, LoopbackHostBindsAnEphemeralLocalPortAndCreatesAFullToken)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebSession session(std::move(loaded.Value()));
    RecentFiles recents;
    Preferences preferences;
    LoopbackHost host(session, recents, preferences);

    const auto endpoint = host.Start();

    ASSERT_TRUE(endpoint.HasValue()) << endpoint.Error().Message();
    EXPECT_GT(endpoint.Value().port, 0U);
    EXPECT_EQ(endpoint.Value().token.size(), 64U);
    EXPECT_EQ(endpoint.Value().Url().find("http://127.0.0.1:"), 0U);
    host.Stop();
}

TEST(WebHttpTest, BrowserLauncherAcceptsOnlyItsOwnLoopbackSessionUrl)
{
    EXPECT_TRUE(IsSafeLocalExplorerUrl("http://127.0.0.1:49152/#token=" + std::string(64U, 'a')));
    EXPECT_FALSE(IsSafeLocalExplorerUrl("https://example.com/#token=" + std::string(64U, 'a')));
    EXPECT_FALSE(IsSafeLocalExplorerUrl("http://127.0.0.1:49152/;bad#token=" + std::string(64U, 'a')));
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
