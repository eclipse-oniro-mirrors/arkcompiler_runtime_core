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

#include "web/host/pick_controller.h"
#include "web/host/request_router.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <future>
#include <string>
#include <thread>

namespace panda::bytecode_explorer::test {
namespace {

// HTTP status codes asserted by the route tests in this file.
constexpr int HTTP_STATUS_OK = 200;
constexpr int HTTP_STATUS_ACCEPTED = 202;
constexpr int HTTP_STATUS_BAD_REQUEST = 400;
constexpr int HTTP_STATUS_UNAUTHORIZED = 401;
constexpr int HTTP_STATUS_FORBIDDEN = 403;
constexpr int HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415;

constexpr uint16_t ROUTER_TEST_PORT = 57428U;
// AwaitTerminalState() polls QueryState() until it leaves "picking"; the
// attempt cap together with the interval bounds the total wait to ~2 seconds.
constexpr int STATE_POLL_MAX_ATTEMPTS = 400;
constexpr std::chrono::milliseconds STATE_POLL_INTERVAL {5};

struct ReleaseGate {
    std::promise<void> &promise;
    bool released {false};
    void Release()
    {
        if (!released) {
            released = true;
            promise.set_value();
        }
    }
    ~ReleaseGate()
    {
        Release();
    }
};

PickController::State AwaitTerminalState(const PickController &pick)
{
    for (int attempt = 0; attempt < STATE_POLL_MAX_ATTEMPTS; ++attempt) {
        const auto state = pick.QueryState();
        if (state.name != "picking") {
            return state;
        }
        std::this_thread::sleep_for(STATE_POLL_INTERVAL);
    }
    return pick.QueryState();
}

LoadedDocument LoadTestDocument()
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    EXPECT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    return std::move(loaded).Value();
}

PickController::DialogOpener SelectingDialog(std::string path)
{
    return [path = std::move(path)] { return FileDialogResult {FileDialogOutcome::SELECTED, path, {}}; };
}

TEST(PickControllerTest, SelectedPickSwitchesTheSessionDocument)
{
    WebSession session(LoadTestDocument());
    const auto firstDocument = session.Api()->DocumentJson().Value();
    PickController pick(session, SelectingDialog(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC));

    EXPECT_EQ(pick.QueryState().name, "idle");
    ASSERT_TRUE(pick.StartPick());
    EXPECT_EQ(pick.QueryState().name, "picking");

    const auto state = AwaitTerminalState(pick);
    ASSERT_EQ(state.name, "selected");
    EXPECT_EQ(state.path, BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC);
    EXPECT_TRUE(state.error.empty());
    session.Wait();
    ASSERT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_NE(session.Api()->DocumentJson().Value(), firstDocument);
}

TEST(PickControllerTest, CancelledPickLeavesTheSessionUntouched)
{
    WebSession session(LoadTestDocument());
    const auto previousApi = session.Api();
    PickController pick(session, [] { return FileDialogResult {FileDialogOutcome::CANCELLED, {}, {}}; });

    ASSERT_TRUE(pick.StartPick());
    const auto state = AwaitTerminalState(pick);
    EXPECT_EQ(state.name, "cancelled");
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_EQ(session.Api(), previousApi);
}

TEST(PickControllerTest, FailedPickReportsTheDialogError)
{
    WebSession session(LoadTestDocument());
    PickController pick(session, [] { return FileDialogResult {FileDialogOutcome::FAILED, {}, "no dialog tool"}; });

    ASSERT_TRUE(pick.StartPick());
    const auto state = AwaitTerminalState(pick);
    EXPECT_EQ(state.name, "failed");
    EXPECT_EQ(state.error, "no dialog tool");
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
}

TEST(PickControllerTest, SecondPickWhileADialogIsOpenIsRejected)
{
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    WebSession session(LoadTestDocument());
    PickController pick(session, [releaseFuture]() mutable {
        releaseFuture.wait();
        return FileDialogResult {FileDialogOutcome::CANCELLED, {}, {}};
    });
    ReleaseGate gate {release};

    ASSERT_TRUE(pick.StartPick());
    EXPECT_FALSE(pick.StartPick());
    EXPECT_EQ(pick.QueryState().name, "picking");
    gate.Release();
    EXPECT_EQ(AwaitTerminalState(pick).name, "cancelled");
    EXPECT_TRUE(pick.StartPick());
    EXPECT_EQ(pick.QueryState().name, "picking");
}

TEST(PickControllerTest, OpenPathSwitchesValidatesAndRejectsBusyStates)
{
    WebSession session(LoadTestDocument());
    const auto firstDocument = session.Api()->DocumentJson().Value();
    std::promise<void> release;
    auto releaseFuture = release.get_future().share();
    PickController pick(session, [releaseFuture]() mutable {
        releaseFuture.wait();
        return FileDialogResult {FileDialogOutcome::CANCELLED, {}, {}};
    });
    ReleaseGate gate {release};

    EXPECT_EQ(pick.OpenPath(""), PickController::OpenStatus::INVALID);
    EXPECT_EQ(pick.OpenPath(std::string(5000U, 'x')), PickController::OpenStatus::INVALID);
    EXPECT_EQ(pick.OpenPath(std::string("bad\0path", 8U)), PickController::OpenStatus::INVALID);

    ASSERT_EQ(pick.OpenPath(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC), PickController::OpenStatus::ACCEPTED);
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_NE(session.Api()->DocumentJson().Value(), firstDocument);

    ASSERT_TRUE(pick.StartPick());
    EXPECT_EQ(pick.OpenPath(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC), PickController::OpenStatus::BUSY);
    gate.Release();
    AwaitTerminalState(pick);
}

TEST(PickControllerRouterTest, PickRoutesRequireAuthAndALoopbackHost)
{
    WebSession session(LoadTestDocument());
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session, SelectingDialog(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC));
    const std::string token(64U, 'p');
    RequestRouter router(session, token, pick, recents, preferences, ROUTER_TEST_PORT);

    HttpRequest request;
    request.method = "POST";
    request.path = "/api/v1/pick";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED);
    request.headers["authorization"] = "Bearer " + token;
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_FORBIDDEN);

    request.headers["host"] = "127.0.0.1:" + std::to_string(ROUTER_TEST_PORT);
    auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_ACCEPTED);
    EXPECT_EQ(nlohmann::json::parse(response.body).at("state"), "picking");

    request.method = "GET";
    request.path = "/api/v1/pick/status";
    response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_OK);
    EXPECT_EQ(nlohmann::json::parse(response.body).at("state"), "picking");

    EXPECT_EQ(AwaitTerminalState(pick).name, "selected");
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);

    request.path = "/api/v1/pick/status";
    response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_OK);
    const auto status = nlohmann::json::parse(response.body);
    EXPECT_EQ(status.at("state"), "selected");
    EXPECT_EQ(status.at("path"), BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC);
}

TEST(PickControllerRouterTest, OpenRouteValidatesTheRequestBodyAndSwitches)
{
    WebSession session(LoadTestDocument());
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session, SelectingDialog("unused"));
    const std::string token(64U, 'o');
    RequestRouter router(session, token, pick, recents, preferences, ROUTER_TEST_PORT);

    HttpRequest request;
    request.method = "POST";
    request.path = "/api/v1/open";
    request.headers["authorization"] = "Bearer " + token;
    request.headers["host"] = "localhost:" + std::to_string(ROUTER_TEST_PORT);

    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
    request.headers["content-type"] = "application/json";

    request.body = "{not json";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.body = R"({"path": 42})";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.body = R"({"path": ""})";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);
    request.body = R"({"path": "a.abc", "extra": 1})";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_BAD_REQUEST);

    request.body = R"({"path": ")" + std::string(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC) + R"("})";
    auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_ACCEPTED);
    EXPECT_EQ(nlohmann::json::parse(response.body).at("state"), "switching");
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_NE(session.Api(), nullptr);
}

TEST(PickControllerRouterTest, OpenRouteRecordsRecentFilesOnlyForOpenedDocuments)
{
    WebSession session(LoadTestDocument());
    RecentFiles recents;
    Preferences preferences;
    // Wired the same way RunBrowser wires it: only loads that publish a new
    // document reach the recent list.
    session.SetDocumentOpenedCallback([&recents](const std::string &opened) { recents.Record(opened); });
    PickController pick(session, SelectingDialog("unused"));
    const std::string token(64U, 'r');
    RequestRouter router(session, token, pick, recents, preferences, ROUTER_TEST_PORT);

    HttpRequest request;
    request.method = "POST";
    request.path = "/api/v1/open";
    request.headers["authorization"] = "Bearer " + token;
    request.headers["host"] = "localhost:" + std::to_string(ROUTER_TEST_PORT);
    request.headers["content-type"] = "application/json";

    // A switch the loader cannot complete keeps the previous document and
    // must not leave a broken entry in the recent list.
    request.body = R"({"path": "/does-not-exist.abc"})";
    auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_ACCEPTED);
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_FALSE(session.Status().lastError.empty());
    EXPECT_TRUE(recents.List().empty());

    // A switch that succeeds is recorded.
    request.body = R"({"path": ")" + std::string(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC) + R"("})";
    response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_STATUS_ACCEPTED);
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_TRUE(session.Status().lastError.empty());
    const auto listed = recents.List();
    ASSERT_EQ(listed.size(), 1U);
    EXPECT_EQ(listed.front().path, BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
