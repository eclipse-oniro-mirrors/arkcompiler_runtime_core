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

#include "web/api/recent_files.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

std::string FileNameOf(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1U);
}

int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

RecentFiles::RecentFiles(std::string storagePath) : storagePath_(std::move(storagePath))
{
    Load();
}

void RecentFiles::Record(std::string path)
{
    if (path.empty()) {
        return;
    }
    std::string fileName = FileNameOf(path);
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&path](const RecentFileEntry &entry) { return entry.path == path; }),
                   entries_.end());
    entries_.insert(entries_.begin(), RecentFileEntry {std::move(path), std::move(fileName), NowMs()});
    if (entries_.size() > MAX_ENTRIES) {
        entries_.resize(MAX_ENTRIES);
    }
    Save();
}

std::vector<RecentFileEntry> RecentFiles::List() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RecentFileEntry> existing;
    existing.reserve(entries_.size());
    std::error_code error;
    for (const auto &entry : entries_) {
        if (std::filesystem::exists(entry.path, error)) {
            existing.push_back(entry);
        }
    }
    return existing;
}

std::string RecentFiles::DefaultStoragePath()
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
    return (directory / "recent.json").string();
}

void RecentFiles::Load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
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
        const auto entries = parsed.find("entries");
        if (entries == parsed.end() || !entries->is_array()) {
            return;
        }
        for (const auto &entry : *entries) {
            if (!entry.is_object() || entry.size() != 3U) {
                continue;
            }
            const auto path = entry.find("path");
            const auto fileName = entry.find("fileName");
            const auto openedAt = entry.find("openedAt");
            if (path == entry.end() || fileName == entry.end() || openedAt == entry.end() ||
                !path->is_string() || !fileName->is_string() || !openedAt->is_number_integer() ||
                path->get<std::string>().empty()) {
                continue;
            }
            entries_.push_back(
                RecentFileEntry {path->get<std::string>(), fileName->get<std::string>(),
                                 openedAt->get<int64_t>()});
        }
        if (entries_.size() > MAX_ENTRIES) {
            entries_.resize(MAX_ENTRIES);
        }
    } catch (...) {
        // A corrupted recent list starts empty; it is not worth failing over.
        entries_.clear();
    }
}

void RecentFiles::Save() const
{
    if (storagePath_.empty()) {
        return;
    }
    try {
        nlohmann::json entries = nlohmann::json::array();
        for (const auto &entry : entries_) {
            entries.push_back({{"path", entry.path}, {"fileName", entry.fileName}, {"openedAt", entry.openedAtMs}});
        }
        const nlohmann::json document = {{"apiVersion", 1}, {"entries", std::move(entries)}};
        std::ofstream output(storagePath_, std::ios::binary | std::ios::trunc);
        output << document.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (...) {
        // Losing the recent list on a write error is acceptable.
    }
}

}  // namespace panda::bytecode_explorer
