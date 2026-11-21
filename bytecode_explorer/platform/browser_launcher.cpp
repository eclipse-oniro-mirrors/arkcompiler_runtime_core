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

#include "platform/browser_launcher.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#endif

namespace panda::bytecode_explorer {
namespace {

constexpr std::string_view URL_PREFIX = "http://127.0.0.1:";
constexpr std::string_view TOKEN_SEPARATOR = "/#token=";
constexpr size_t TOKEN_CHARACTER_COUNT = 64U;

bool IsHex(char value)
{
    return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

#if defined(_WIN32)
std::wstring Utf8ToWide(std::string_view text)
{
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(),
                            size) != size) {
        return {};
    }
    return result;
}
#endif

#if !defined(_WIN32)
Status SpawnAndReap(const char *program, std::vector<char *> arguments, int *spawnError = nullptr)
{
    pid_t process = 0;
    const int status = posix_spawnp(&process, program, nullptr, nullptr, arguments.data(), environ);
    if (spawnError != nullptr) {
        *spawnError = status;
    }
    if (status != 0) {
        return Status(ErrorCode::PLATFORM_ERROR,
                      "Cannot start " + std::string(program) + ": " + std::to_string(status));
    }
    try {
        std::thread([process]() {
            int childStatus = 0;
            while (waitpid(process, &childStatus, 0) < 0 && errno == EINTR) {
            }
        }).detach();
    } catch (...) {
        int childStatus = 0;
        while (waitpid(process, &childStatus, 0) < 0 && errno == EINTR) {
        }
    }
    return {};
}
#endif

}  // namespace

bool IsSafeLocalExplorerUrl(std::string_view url)
{
    if (url.rfind(URL_PREFIX, 0U) != 0U) {
        return false;
    }
    const auto separator = url.find(TOKEN_SEPARATOR, URL_PREFIX.size());
    if (separator == std::string_view::npos) {
        return false;
    }
    const auto portText = url.substr(URL_PREFIX.size(), separator - URL_PREFIX.size());
    uint32_t port = 0U;
    const auto portResult = std::from_chars(portText.data(), portText.data() + portText.size(), port);
    if (portText.empty() || portResult.ec != std::errc {} || portResult.ptr != portText.data() + portText.size() ||
        port == 0U || port > UINT16_MAX) {
        return false;
    }
    const auto token = url.substr(separator + TOKEN_SEPARATOR.size());
    return token.size() == TOKEN_CHARACTER_COUNT && std::all_of(token.begin(), token.end(), IsHex);
}

Status BrowserLauncher::Open(std::string_view url) const
{
    if (!IsSafeLocalExplorerUrl(url)) {
        return Status(ErrorCode::INVALID_ARGUMENT, "Refusing to open a non-local bytecode explorer URL");
    }
#if defined(_WIN32)
    const auto wideUrl = Utf8ToWide(url);
    if (wideUrl.empty()) {
        return Status(ErrorCode::PLATFORM_ERROR, "Cannot convert the local browser URL to UTF-16");
    }
    const auto result = ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    // ShellExecuteW reports failure with a return value at or below 32.
    constexpr intptr_t SHELLEXECUTE_ERROR_CODE_MAX = 32;
    if (reinterpret_cast<intptr_t>(result) <= SHELLEXECUTE_ERROR_CODE_MAX) {
        return Status(ErrorCode::PLATFORM_ERROR, "Windows could not open the default browser");
    }
    return {};
#else
#if defined(__APPLE__)
    const char *program = "open";
    std::string ownedUrl(url);
    std::vector<char *> arguments {const_cast<char *>(program), ownedUrl.data(), nullptr};
    return SpawnAndReap(program, std::move(arguments));
#else
    const char *program = "xdg-open";
    std::string ownedUrl(url);
    std::vector<char *> arguments {const_cast<char *>(program), ownedUrl.data(), nullptr};
    int spawnError = 0;
    auto result = SpawnAndReap(program, std::move(arguments), &spawnError);
    if (result.Code() == ErrorCode::NONE || spawnError != ENOENT) {
        return result;
    }

    // WSL images commonly expose Windows PowerShell but do not install xdg-open.
    std::string command = "Start-Process '" + ownedUrl + "'";
    char powershell[] = "powershell.exe";
    char noProfile[] = "-NoProfile";
    char commandOption[] = "-Command";
    std::vector<char *> powershellArguments {powershell, noProfile, commandOption, command.data(), nullptr};
    return SpawnAndReap(powershell, std::move(powershellArguments));
#endif
#endif
}

}  // namespace panda::bytecode_explorer
