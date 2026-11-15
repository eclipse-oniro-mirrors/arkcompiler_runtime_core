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

#ifndef BYTECODE_EXPLORER_WEB_API_PREFERENCES_H
#define BYTECODE_EXPLORER_WEB_API_PREFERENCES_H

#include <mutex>
#include <string>
#include <string_view>

namespace panda::bytecode_explorer {

// Server-side UI preferences (currently the color theme). The explorer port
// changes on every restart, so browser storage (scoped per origin) cannot
// retain them; this file can. Mirrors RecentFiles.
class Preferences final {
public:
    static constexpr std::string_view DEFAULT_THEME = "dark";

    // Empty storage path keeps the preferences in memory only (used by tests
    // and environments without a writable home directory).
    explicit Preferences(std::string storagePath = {});

    std::string Theme() const;

    // Stores the theme when it is a known value; unknown values are rejected
    // so a compromised page cannot smuggle arbitrary strings into the file.
    bool SetTheme(std::string theme);

    static bool IsKnownTheme(std::string_view theme);

    // "$HOME/.cache/ark_bytecode_explorer/preferences.json" (USERPROFILE on
    // Windows); empty when no home directory is set.
    static std::string DefaultStoragePath();

private:
    void Load();
    void Save() const;

    std::string storagePath_;
    mutable std::mutex mutex_;
    std::string theme_ {DEFAULT_THEME};
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_WEB_API_PREFERENCES_H
