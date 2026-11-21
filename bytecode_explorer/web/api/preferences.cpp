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

#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace panda::bytecode_explorer {

namespace {

constexpr std::string_view KNOWN_THEMES[] = {"dark", "light", "high-contrast"};

}  // namespace

Preferences::Preferences(std::string storagePath) : storagePath_(std::move(storagePath))
{
    Load();
}

std::string Preferences::Theme() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return theme_;
}

bool Preferences::SetTheme(std::string theme)
{
    if (!IsKnownTheme(theme)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        theme_ = std::move(theme);
    }
    Save();
    return true;
}

bool Preferences::IsKnownTheme(std::string_view theme)
{
    for (const auto known : KNOWN_THEMES) {
        if (theme == known) {
            return true;
        }
    }
    return false;
}

std::string Preferences::DefaultStoragePath()
{
    const char *home = std::getenv("HOME");
#if defined(_WIN32)
    if (home == nullptr) {
        home = std::getenv("USERPROFILE");
    }
#endif
    if (home == nullptr || *home == '\0') {
        return {};
    }
    std::error_code error;
    const std::filesystem::path directory = std::filesystem::path(home) / ".cache" / "ark_bytecode_explorer";
    if (!std::filesystem::exists(directory, error)) {
        if (!std::filesystem::create_directories(directory, error)) {
            return {};
        }
    }
    if (!std::filesystem::is_directory(directory, error)) {
        return {};
    }
    return (directory / "preferences.json").string();
}

void Preferences::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    theme_ = std::string(DEFAULT_THEME);
    if (storagePath_.empty()) {
        return;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(storagePath_, error)) {
        return;
    }
    try {
        std::ifstream input(storagePath_, std::ios::binary);
        const auto parsed = nlohmann::json::parse(input, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return;
        }
        const auto theme = parsed.find("theme");
        if (theme == parsed.end() || !theme->is_string() || !IsKnownTheme(theme->get<std::string>())) {
            return;
        }
        theme_ = theme->get<std::string>();
    } catch (...) {
        // Corrupted preferences fall back to the default theme.
        theme_ = std::string(DEFAULT_THEME);
    }
}

void Preferences::Save() const
{
    if (storagePath_.empty()) {
        return;
    }
    try {
        const nlohmann::json document = {{"apiVersion", 1}, {"theme", Theme()}};
        std::ofstream output(storagePath_, std::ios::binary | std::ios::trunc);
        output << document.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (...) {
        // Losing the preference on a write error is acceptable.
    }
}

}  // namespace panda::bytecode_explorer
