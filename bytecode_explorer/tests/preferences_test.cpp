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

#include "web/api/preferences.h"
#include "web/host/request_router.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace panda::bytecode_explorer::test {
namespace {

// HTTP status codes asserted by the router tests below.
constexpr int HTTP_OK = 200;
constexpr int HTTP_BAD_REQUEST = 400;
constexpr int HTTP_FORBIDDEN = 403;
constexpr int HTTP_UNSUPPORTED_MEDIA_TYPE = 415;

class PreferencesTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        storage_ = "preferences_test_" + std::to_string(::getpid()) + ".json";
        std::filesystem::remove(storage_);
    }

    void TearDown() override
    {
        std::filesystem::remove(storage_);
    }

    void WriteStorage(const std::string &content)
    {
        std::ofstream output(storage_, std::ios::binary | std::ios::trunc);
        output << content;
    }

    std::string storage_;
};

TEST_F(PreferencesTest, DefaultsToDarkTheme)
{
    const Preferences preferences(storage_);
    EXPECT_EQ(preferences.Theme(), "dark");
}

TEST_F(PreferencesTest, PersistsKnownThemesAcrossInstances)
{
    {
        Preferences preferences(storage_);
        EXPECT_TRUE(preferences.SetTheme("light"));
    }
    {
        Preferences reloaded(storage_);
        EXPECT_EQ(reloaded.Theme(), "light");
        EXPECT_TRUE(reloaded.SetTheme("high-contrast"));
    }
    {
        const Preferences reloaded(storage_);
        EXPECT_EQ(reloaded.Theme(), "high-contrast");
    }
}

TEST_F(PreferencesTest, RejectsUnknownThemesWithoutSaving)
{
    Preferences preferences(storage_);
    EXPECT_TRUE(preferences.SetTheme("light"));
    EXPECT_TRUE(preferences.SetTheme("high-contrast"));
    EXPECT_FALSE(preferences.SetTheme("solarized"));
    EXPECT_FALSE(preferences.SetTheme(""));
    EXPECT_EQ(preferences.Theme(), "high-contrast");
}

TEST_F(PreferencesTest, IgnoresCorruptedOrUnknownStoredThemes)
{
    WriteStorage("{not json at all");
    EXPECT_EQ(Preferences(storage_).Theme(), "dark");

    WriteStorage(R"({"theme": "monokai"})");
    EXPECT_EQ(Preferences(storage_).Theme(), "dark");

    WriteStorage(R"({"theme": 42})");
    EXPECT_EQ(Preferences(storage_).Theme(), "dark");
}

TEST_F(PreferencesTest, EmptyStoragePathKeepsThePreferenceInMemoryOnly)
{
    const Preferences preferences;
    EXPECT_EQ(preferences.Theme(), "dark");
    EXPECT_TRUE(Preferences().SetTheme("light"));
    EXPECT_FALSE(std::filesystem::exists("preferences.json"));
}

TEST_F(PreferencesTest, RouterServesAndUpdatesTheTheme)
{
    // The preferences routes sit before the session-state gate, so an empty
    // session (no document ever loaded) still serves them.
    WebSession session("preferences_test_no_such.abc");
    RecentFiles recents;
    Preferences preferences;
    PickController pick(session);
    const std::string token(64U, 'p');
    RequestRouter router(session, token, pick, recents, preferences, 57430U);

    HttpRequest request;
    request.headers["authorization"] = "Bearer " + token;
    request.headers["host"] = "localhost:57430";

    request.method = "GET";
    request.path = "/api/v1/preferences";
    auto response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_OK);
    EXPECT_EQ(nlohmann::json::parse(response.body).at("theme"), "dark");

    request.method = "PUT";
    request.headers["content-type"] = "text/plain";
    EXPECT_EQ(router.Route(request).status, HTTP_UNSUPPORTED_MEDIA_TYPE);
    request.headers["content-type"] = "application/json";

    request.body = "{not json";
    EXPECT_EQ(router.Route(request).status, HTTP_BAD_REQUEST);
    request.body = R"({"theme": 42})";
    EXPECT_EQ(router.Route(request).status, HTTP_BAD_REQUEST);
    request.body = R"({"theme": "solarized"})";
    EXPECT_EQ(router.Route(request).status, HTTP_BAD_REQUEST);
    request.body = R"({"theme": "light", "extra": 1})";
    EXPECT_EQ(router.Route(request).status, HTTP_BAD_REQUEST);
    EXPECT_EQ(preferences.Theme(), "dark");

    request.body = R"({"theme": "light"})";
    response = router.Route(request);
    ASSERT_EQ(response.status, HTTP_OK);
    EXPECT_EQ(nlohmann::json::parse(response.body).at("theme"), "light");
    EXPECT_EQ(preferences.Theme(), "light");

    // The write side is a local side effect: it needs the loopback Host.
    request.headers["host"] = "evil.example:57430";
    request.body = R"({"theme": "dark"})";
    EXPECT_EQ(router.Route(request).status, HTTP_FORBIDDEN);
    EXPECT_EQ(preferences.Theme(), "light");
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
