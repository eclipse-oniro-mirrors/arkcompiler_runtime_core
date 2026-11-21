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

#include "web/api/web_session.h"
#include "web/host/pick_controller.h"
#include "web/host/request_router.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace panda::bytecode_explorer::test {
namespace {

// HTTP status codes asserted by the route tests in this file.
constexpr int HTTP_STATUS_OK = 200;
constexpr int HTTP_STATUS_UNAUTHORIZED = 401;
constexpr int HTTP_STATUS_SERVICE_UNAVAILABLE = 503;

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

TEST(WebSessionTest, PublishesOnlyCompleteApiWithoutBlockingStatusAndAssets)
{
    auto entered = std::make_shared<std::promise<void>>();
    std::promise<void> release;
    auto enteredFuture = entered->get_future();
    auto releaseFuture = release.get_future().share();
    WebSession session(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC,
                       [entered, releaseFuture](const std::string &path, const DocumentLoadOptions &options) {
                           options.onStage(DocumentLoadStage::METHODS);
                           entered->set_value();
                           releaseFuture.wait();
                           return LoadExplorerDocument(path, options);
                       });
    ReleaseGate gate {release};
    ASSERT_TRUE(session.Start());
    EXPECT_FALSE(session.Start());
    ASSERT_EQ(enteredFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(session.Status().stage, "methods");
    EXPECT_EQ(session.Api(), nullptr);
    const std::string token(64U, 'c');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.path = "/";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    request.path = "/api/v1/session/status";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_UNAUTHORIZED);
    request.headers["authorization"] = "Bearer " + token;
    auto response = router.Route(request);
    EXPECT_EQ(response.status, HTTP_STATUS_OK);
    EXPECT_EQ(nlohmann::json::parse(response.body).at("stage"), "methods");
    request.path = "/api/v1/document";
    response = router.Route(request);
    EXPECT_EQ(response.status, HTTP_STATUS_SERVICE_UNAVAILABLE);
    EXPECT_EQ(response.Header("Retry-After"), "1");
    gate.Release();
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    ASSERT_NE(session.Api(), nullptr);
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
    EXPECT_TRUE(session.Api()->StructureJson().HasValue());
}

TEST(WebSessionTest, ReportsLoadFailuresAndWorkerExceptions)
{
    WebSession missing("/does-not-exist.abc");
    ASSERT_TRUE(missing.Start());
    missing.Wait();
    EXPECT_EQ(missing.Status().state, WebSessionState::FAILED);
    EXPECT_FALSE(missing.Status().error.empty());
    EXPECT_EQ(missing.Api(), nullptr);
    EXPECT_EQ(nlohmann::json::parse(missing.StatusJson()).at("state"), "failed");

    WebSession throwing("unused", [](const std::string &, const DocumentLoadOptions &) -> LoadedDocumentResult {
        throw std::runtime_error("test loader failure");
    });
    ASSERT_TRUE(throwing.Start());
    throwing.Wait();
    EXPECT_EQ(throwing.Status().state, WebSessionState::FAILED);
    EXPECT_EQ(throwing.Status().error, "test loader failure");
}

TEST(WebSessionTest, CancelledWorkCannotPublishAReadyDocument)
{
    auto entered = std::make_shared<std::promise<void>>();
    std::promise<void> release;
    auto enteredFuture = entered->get_future();
    auto releaseFuture = release.get_future().share();
    WebSession session(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC,
                       [entered, releaseFuture](const std::string &path, const DocumentLoadOptions &) {
                           entered->set_value();
                           releaseFuture.wait();
                           return LoadExplorerDocument(path);
                       });
    ReleaseGate gate {release};
    ASSERT_TRUE(session.Start());
    ASSERT_EQ(enteredFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    session.Cancel();
    gate.Release();
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::CANCELLED);
    EXPECT_EQ(session.Api(), nullptr);
    EXPECT_FALSE(session.Start());
    EXPECT_EQ(nlohmann::json::parse(session.StatusJson()).at("state"), "cancelled");
}

TEST(WebSessionTest, CancelBeforeStartDoesNotInvokeLoader)
{
    auto called = std::make_shared<bool>(false);
    WebSession session("unused", [called](const std::string &, const DocumentLoadOptions &) {
        *called = true;
        return LoadedDocumentResult::Failure(panda::bytecode_explorer::Status(ErrorCode::BACKEND_ERROR, "unexpected"));
    });
    session.Cancel();
    EXPECT_FALSE(session.Start());
    session.Wait();
    EXPECT_FALSE(*called);
    EXPECT_EQ(session.Status().state, WebSessionState::CANCELLED);
}

TEST(WebSessionTest, ReloadSwapsDocumentAndKeepsThePreviousApiDuringLoading)
{
    auto reloadEntered = std::make_shared<std::promise<void>>();
    std::promise<void> reloadRelease;
    auto reloadEnteredFuture = reloadEntered->get_future();
    auto reloadReleaseFuture = reloadRelease.get_future().share();
    const std::string secondPath = BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC;
    const auto loadWithReloadGate = [reloadEntered, reloadReleaseFuture,
                                     secondPath](const std::string &path, const DocumentLoadOptions &options) {
        if (path == secondPath) {
            reloadEntered->set_value();
            reloadReleaseFuture.wait();
        }
        return LoadExplorerDocument(path, options);
    };
    WebSession session(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC, loadWithReloadGate);
    ReleaseGate gate {reloadRelease};
    ASSERT_TRUE(session.Start());
    session.Wait();
    ASSERT_EQ(session.Status().state, WebSessionState::READY);
    const auto firstApi = session.Api();
    ASSERT_NE(firstApi, nullptr);
    const auto firstDocument = firstApi->DocumentJson().Value();

    ASSERT_TRUE(session.Reload(secondPath));
    EXPECT_FALSE(session.Reload(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC));  // busy while reloading
    ASSERT_EQ(reloadEnteredFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(session.Status().state, WebSessionState::LOADING);
    EXPECT_EQ(session.Status().stage, "opening");
    EXPECT_EQ(session.Api(), firstApi);  // the previous document stays published
    gate.Release();
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    const auto secondApi = session.Api();
    ASSERT_NE(secondApi, nullptr);
    EXPECT_NE(secondApi, firstApi);
    EXPECT_NE(secondApi->DocumentJson().Value(), firstDocument);
    const auto status = nlohmann::json::parse(session.StatusJson());
    EXPECT_EQ(status.at("state"), "ready");
    EXPECT_TRUE(status.at("lastError").is_null());
}

TEST(WebSessionTest, FailedReloadKeepsServingThePreviousDocument)
{
    WebSession session(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(session.Start());
    session.Wait();
    ASSERT_EQ(session.Status().state, WebSessionState::READY);
    const auto previousApi = session.Api();
    ASSERT_NE(previousApi, nullptr);

    ASSERT_TRUE(session.Reload("/does-not-exist.abc"));
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_EQ(session.Api(), previousApi);
    const auto status = nlohmann::json::parse(session.StatusJson());
    EXPECT_EQ(status.at("state"), "ready");
    ASSERT_TRUE(status.at("lastError").is_string());
    EXPECT_FALSE(status.at("lastError").get<std::string>().empty());

    // A later successful reload clears the reported switch failure.
    ASSERT_TRUE(session.Reload(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC));
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_TRUE(nlohmann::json::parse(session.StatusJson()).at("lastError").is_null());
}

TEST(WebSessionTest, CancelledReloadKeepsThePreviousDocument)
{
    auto reloadEntered = std::make_shared<std::promise<void>>();
    std::promise<void> reloadRelease;
    auto reloadEnteredFuture = reloadEntered->get_future();
    auto reloadReleaseFuture = reloadRelease.get_future().share();
    const auto loadWithReloadGate = [reloadEntered, reloadReleaseFuture](const std::string &path,
                                                                         const DocumentLoadOptions &options) {
        if (path == BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC) {
            reloadEntered->set_value();
            reloadReleaseFuture.wait();
        }
        return LoadExplorerDocument(path, options);
    };
    WebSession session(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC, loadWithReloadGate);
    ReleaseGate gate {reloadRelease};
    ASSERT_TRUE(session.Start());
    session.Wait();
    ASSERT_EQ(session.Status().state, WebSessionState::READY);
    const auto previousApi = session.Api();
    ASSERT_NE(previousApi, nullptr);

    ASSERT_TRUE(session.Reload(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC));
    ASSERT_EQ(reloadEnteredFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    session.Cancel();
    gate.Release();
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_EQ(session.Api(), previousApi);
    EXPECT_EQ(nlohmann::json::parse(session.StatusJson()).at("lastError"), "ABC loading was cancelled");
}

TEST(WebSessionTest, EmptySessionServesNoDocumentUntilReload)
{
    WebSession session("");
    EXPECT_EQ(session.Status().state, WebSessionState::EMPTY);
    EXPECT_EQ(session.Status().stage, "empty");
    EXPECT_FALSE(session.Start());
    EXPECT_EQ(session.Api(), nullptr);
    const auto emptyStatus = nlohmann::json::parse(session.StatusJson());
    EXPECT_EQ(emptyStatus.at("state"), "empty");
    EXPECT_EQ(emptyStatus.at("stage"), "empty");

    const std::string token(64U, 'c');
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    RequestRouter router(session, token, pick, recents, preferences, 57428U);
    HttpRequest request;
    request.method = "GET";
    request.headers["authorization"] = "Bearer " + token;
    request.path = "/api/v1/session/status";
    auto response = router.Route(request);
    EXPECT_EQ(response.status, HTTP_STATUS_OK);
    EXPECT_EQ(nlohmann::json::parse(response.body).at("state"), "empty");
    request.path = "/api/v1/document";
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_SERVICE_UNAVAILABLE);

    ASSERT_TRUE(session.Reload(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC));
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    ASSERT_NE(session.Api(), nullptr);
    EXPECT_EQ(router.Route(request).status, HTTP_STATUS_OK);
}

TEST(WebSessionTest, DocumentOpenedCallbackSkipsFailedLoads)
{
    auto callbackMutex = std::make_shared<std::mutex>();
    auto openedPaths = std::make_shared<std::vector<std::string>>();
    const auto failOnMissingPath = [](const std::string &path, const DocumentLoadOptions &options) {
        if (path.find("missing") != std::string::npos) {
            return LoadedDocumentResult::Failure(Status(ErrorCode::UNSUPPORTED_FORMAT, "cannot open " + path));
        }
        return LoadExplorerDocument(path, options);
    };
    WebSession session(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC, failOnMissingPath);
    session.SetDocumentOpenedCallback([callbackMutex, openedPaths](const std::string &path) {
        std::lock_guard<std::mutex> lock(*callbackMutex);
        openedPaths->push_back(path);
    });

    // The initial load succeeds and notifies exactly once.
    ASSERT_TRUE(session.Start());
    session.Wait();
    ASSERT_EQ(session.Status().state, WebSessionState::READY);
    ASSERT_EQ(openedPaths->size(), 1U);
    EXPECT_EQ(openedPaths->front(), BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);

    // A reload that fails keeps the previous document and never notifies.
    ASSERT_TRUE(session.Reload("/missing.abc"));
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_FALSE(session.Status().lastError.empty());
    EXPECT_EQ(openedPaths->size(), 1U);

    // A reload that succeeds notifies with the newly opened path.
    ASSERT_TRUE(session.Reload(BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC));
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    EXPECT_TRUE(session.Status().lastError.empty());
    ASSERT_EQ(openedPaths->size(), 2U);
    EXPECT_EQ(openedPaths->back(), BYTECODE_EXPLORER_STRUCTURE_NONMERGE_TEST_ABC);
}

TEST(WebSessionTest, DocumentOpenedCallbackFailuresDoNotFailTheSession)
{
    WebSession session(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    session.SetDocumentOpenedCallback([](const std::string &) { throw std::runtime_error("callback boom"); });

    ASSERT_TRUE(session.Start());
    session.Wait();
    EXPECT_EQ(session.Status().state, WebSessionState::READY);
    ASSERT_NE(session.Api(), nullptr);
    EXPECT_TRUE(session.Status().lastError.empty());
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
