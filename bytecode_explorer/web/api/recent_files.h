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

#ifndef BYTECODE_EXPLORER_WEB_API_RECENT_FILES_H
#define BYTECODE_EXPLORER_WEB_API_RECENT_FILES_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace panda::bytecode_explorer {

struct RecentFileEntry {
    std::string path;
    std::string fileName;
    int64_t openedAtMs {0};
};

// Server-side recent ABC list. The explorer port changes on every restart, so
// browser storage (scoped per origin) cannot retain recents; this file can.
// Entries whose file no longer exists are dropped when listing.
class RecentFiles final {
public:
    static constexpr size_t MAX_ENTRIES = 8U;

    // Empty storage path keeps the list in memory only (used by tests and
    // environments without a writable home directory).
    explicit RecentFiles(std::string storagePath = {});

    // Records a successfully opened document with the current time: moves an
    // existing entry to the front. Callers feed it from the session's
    // document-opened callback, so failed opens never reach the list.
    void Record(std::string path);

    std::vector<RecentFileEntry> List() const;

    // "$HOME/.cache/ark_bytecode_explorer/recent.json" (USERPROFILE on Windows);
    // empty when no home directory is set.
    static std::string DefaultStoragePath();

private:
    void Load();
    void Save() const;

    std::string storagePath_;
    mutable std::mutex mutex_;
    std::vector<RecentFileEntry> entries_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_WEB_API_RECENT_FILES_H
