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

#include "platform/file_dialog.h"

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#else
#include <sys/wait.h>

#include <cstdio>
#endif

#include <algorithm>
#include <cctype>
#include <string>

namespace panda::bytecode_explorer {
namespace {

#if defined(_WIN32)

std::string WideToUtf8(const wchar_t *text)
{
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size - 1), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), size, nullptr, nullptr) != size) {
        return {};
    }
    return result;
}

FileDialogResult OpenOnWindows()
{
    // Long paths need more than MAX_PATH; the buffer size is the only limit.
    std::wstring file(32768U, L'\0');
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"ABC bytecode (*.abc)\0*.abc\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrTitle = L"Open ABC bytecode";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == 0) {
        if (CommDlgExtendedError() == 0) {
            return {FileDialogOutcome::CANCELLED, {}, {}};
        }
        return {FileDialogOutcome::FAILED, {}, "The Windows file dialog failed to open"};
    }
    auto path = WideToUtf8(file.c_str());
    if (path.empty()) {
        return {FileDialogOutcome::FAILED, {}, "Cannot convert the picked file path to UTF-8"};
    }
    return {FileDialogOutcome::SELECTED, std::move(path), {}};
}

#else

std::string Trimmed(std::string value)
{
    const auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };
    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return begin < end ? std::string(begin, end) : std::string();
}

// Runs a dialog command and classifies its exit status. Every helper below uses
// the same convention: 0 = a path was printed on stdout, 1 = the user cancelled,
// TOOL_NOT_FOUND_EXIT_CODE = the tool is not installed.
constexpr int TOOL_NOT_FOUND_EXIT_CODE = 127;

struct CommandOutput {
    int exitCode {-1};
    bool failed {false};
    bool toolMissing {false};
    std::string output;
};

CommandOutput RunCommand(const std::string &command)
{
    CommandOutput result;
    FILE *stream = popen(command.c_str(), "r");
    if (stream == nullptr) {
        result.failed = true;
        return result;
    }
    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), stream) != nullptr) {
        output.append(buffer);
    }
    const int status = pclose(stream);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        result.toolMissing = result.exitCode == TOOL_NOT_FOUND_EXIT_CODE;
        result.failed = result.exitCode != 0 && result.exitCode != 1 && !result.toolMissing;
    } else {
        result.failed = true;
    }
    result.output = std::move(output);
    return result;
}

FileDialogResult SelectedOrCancelled(const CommandOutput &run)
{
    const auto path = Trimmed(run.output);
    if (path.empty()) {
        return {FileDialogOutcome::CANCELLED, {}, {}};
    }
    return {FileDialogOutcome::SELECTED, path, {}};
}

FileDialogResult DialogResultFrom(const CommandOutput &run, std::string_view toolName)
{
    if (run.failed) {
        return {FileDialogOutcome::FAILED, {},
                std::string(toolName) + " failed with exit code " + std::to_string(run.exitCode)};
    }
    if (run.exitCode == 1) {
        return {FileDialogOutcome::CANCELLED, {}, {}};
    }
    return SelectedOrCancelled(run);
}

#if !defined(__APPLE__)

bool IsWslEnvironment()
{
    FILE *stream = fopen("/proc/version", "r");
    if (stream == nullptr) {
        return false;
    }
    std::string content;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), stream) != nullptr) {
        content.append(buffer);
    }
    // The stream is read-only, so fclose has nothing to flush; its status is ignored.
    (void)fclose(stream);
    std::transform(content.begin(), content.end(), content.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return content.find("microsoft") != std::string::npos;
}

struct ToolRun {
    FileDialogResult result;
    bool toolMissing {false};
};

ToolRun OpenWithZenity()
{
    const auto run = RunCommand(
        "zenity --file-selection --title='Open ABC bytecode' --file-filter='ABC bytecode | *.abc' 2>/dev/null");
    if (run.toolMissing) {
        return {};
    }
    return {DialogResultFrom(run, "zenity")};
}

ToolRun OpenWithKdialog()
{
    const auto run = RunCommand("kdialog --getopenfilename . '*.abc | ABC bytecode' 2>/dev/null");
    if (run.toolMissing) {
        return {};
    }
    return {DialogResultFrom(run, "kdialog")};
}

ToolRun OpenWithPowerShell()
{
    // The shell expands $d inside double quotes, so every PowerShell variable is
    // escaped for /bin/sh. Writing through [Console]::Out keeps the path on one
    // UTF-8 line regardless of the Windows console code page.
    const auto run = RunCommand(
        "powershell.exe -NoProfile -STA -Command \"[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "Add-Type -AssemblyName System.Windows.Forms; "
        "\\$d = New-Object System.Windows.Forms.OpenFileDialog; "
        "\\$d.Filter = 'ABC bytecode (*.abc)|*.abc|All files (*.*)|*.*'; "
        "\\$d.Title = 'Open ABC bytecode'; "
        "if (\\$d.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 }; "
        "[Console]::Out.Write(\\$d.FileName)\" 2>/dev/null");
    if (run.toolMissing) {
        return {};
    }
    if (run.exitCode == 0 && !run.failed) {
        const auto windowsPath = Trimmed(run.output);
        if (windowsPath.empty()) {
            return {FileDialogResult {FileDialogOutcome::CANCELLED, {}, {}}};
        }
        auto path = WslPathFromWindowsPath(windowsPath);
        if (path.empty()) {
            return {FileDialogResult {FileDialogOutcome::FAILED, {},
                                      "The picked file is not under a Windows drive mounted inside WSL"}};
        }
        return {FileDialogResult {FileDialogOutcome::SELECTED, std::move(path), {}}};
    }
    return {DialogResultFrom(run, "powershell.exe")};
}

FileDialogResult OpenOnLinux()
{
    if (IsWslEnvironment()) {
        const auto powershell = OpenWithPowerShell();
        if (!powershell.toolMissing) {
            return powershell.result;
        }
        // A stripped WSL image without Windows PowerShell may still ship the
        // Linux dialog tools below; fall through instead of failing outright.
    }
    const auto zenity = OpenWithZenity();
    if (!zenity.toolMissing) {
        return zenity.result;
    }
    const auto kdialog = OpenWithKdialog();
    if (!kdialog.toolMissing) {
        return kdialog.result;
    }
    return {FileDialogOutcome::FAILED, {}, "No file dialog tool is available (install zenity)"};
}

#endif  // !defined(__APPLE__)

#endif  // defined(_WIN32)

}  // namespace

// A Windows drive prefix is three characters: drive letter, colon, separator ("C:\" or "C:/").
constexpr size_t WINDOWS_DRIVE_PREFIX_LENGTH = 3U;
constexpr size_t WINDOWS_SEPARATOR_INDEX = 2U;

std::string WslPathFromWindowsPath(std::string_view windowsPath)
{
    if (windowsPath.size() < WINDOWS_DRIVE_PREFIX_LENGTH) {
        return {};
    }
    const auto drive = static_cast<char>(std::tolower(static_cast<unsigned char>(windowsPath[0])));
    if (drive < 'a' || drive > 'z' || windowsPath[1] != ':' ||
        (windowsPath[WINDOWS_SEPARATOR_INDEX] != '\\' && windowsPath[WINDOWS_SEPARATOR_INDEX] != '/')) {
        return {};
    }
    std::string path("/mnt/");
    path += drive;
    for (size_t index = WINDOWS_SEPARATOR_INDEX; index < windowsPath.size(); ++index) {
        path += windowsPath[index] == '\\' ? '/' : windowsPath[index];
    }
    return path;
}

FileDialogResult FileDialog::Open() const
{
#if defined(_WIN32)
    return OpenOnWindows();
#elif defined(__APPLE__)
    const auto run =
        RunCommand("osascript -e 'POSIX path of (choose file with prompt \"Open ABC bytecode\")' 2>/dev/null");
    return DialogResultFrom(run, "osascript");
#else
    return OpenOnLinux();
#endif
}

}  // namespace panda::bytecode_explorer
