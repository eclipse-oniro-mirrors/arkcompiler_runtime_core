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

#ifndef BYTECODE_EXPLORER_PLATFORM_FILE_DIALOG_H
#define BYTECODE_EXPLORER_PLATFORM_FILE_DIALOG_H

#include <string>
#include <string_view>

namespace panda::bytecode_explorer {

enum class FileDialogOutcome { SELECTED, CANCELLED, FAILED };

struct FileDialogResult {
    FileDialogOutcome outcome {FileDialogOutcome::FAILED};
    std::string path;
    std::string error;
};

// Blocking platform file-open dialog. Callers must run it off the HTTP event
// loop: it shows a modal dialog and returns only after the user closed it.
class FileDialog final {
public:
    FileDialogResult Open() const;
};

// Converts a Windows drive path (C:\dir\file.abc) into its WSL mount path
// (/mnt/c/dir/file.abc). Returns an empty string for paths this cannot map.
std::string WslPathFromWindowsPath(std::string_view windowsPath);

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_PLATFORM_FILE_DIALOG_H
