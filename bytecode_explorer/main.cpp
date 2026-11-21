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

#include "web/browser_application.h"
#include "app/command_line.h"
#include "core/document/abc_document.h"
#include "core/format/structure_formatter.h"
#include "core/reader/dynamic_structure_reader.h"

#include <iostream>

#if defined(_WIN32)
#include <windows.h>

#include <limits>
#include <string_view>
#include <vector>
#endif

namespace {

constexpr const char *VERSION = "0.1.0";
// Exit status for unusable command lines: a usage or diagnostic message has been printed.
constexpr int USAGE_ERROR_EXIT_CODE = 2;

void PrintUsage()
{
    std::cout << "Usage: ark_bytecode_explorer [options] [file.abc]\n"
                 "\n"
                 "Opens a local explorer page for file.abc. Without a file the page starts\n"
                 "empty and offers an in-page file picker.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help       Show this help without opening the ABC\n"
                 "  --version        Show version without opening the ABC\n"
                 "  --no-open        Print the local URL without opening a browser\n"
                 "  --dump-structure Print the semantic structure tree and exit\n"
                 "  --show-internals Include compiler-generated and ABC-internal nodes in a structure dump\n";
}

int DumpStructure(const panda::bytecode_explorer::CommandLineOptions &options)
{
    auto document = panda::bytecode_explorer::AbcDocument::Open(*options.abcPath);
    if (!document.HasValue()) {
        std::cerr << document.Error().Message() << '\n';
        return 1;
    }
    auto structure = panda::bytecode_explorer::DynamicStructureReader::Build(document.Value()->Summary().path,
                                                                             document.Value()->File());
    if (!structure.HasValue()) {
        std::cerr << structure.Error().Message() << '\n';
        return 1;
    }
    std::cout << panda::bytecode_explorer::StructureFormatter::Format(*structure.Value(), options.showInternals);
    return 0;
}

int Run(int argc, const char *const *argv)
{
    auto options = panda::bytecode_explorer::ParseCommandLine(argc, argv);
    if (!options.HasValue()) {
        std::cerr << options.Error().Message() << "\n\n";
        PrintUsage();
        return USAGE_ERROR_EXIT_CODE;
    }
    auto parsed = std::move(options.Value());
    if (parsed.showHelp) {
        PrintUsage();
        return 0;
    }
    if (parsed.showVersion) {
        std::cout << "ArkTS Bytecode Explorer " << VERSION << '\n';
        return 0;
    }
    if (parsed.dumpStructure) {
        return DumpStructure(parsed);
    }

    // An empty path starts the explorer with an empty picker page.
    return panda::bytecode_explorer::RunBrowser(parsed.abcPath.value_or(""), !parsed.noOpen);
}

}  // namespace

#if defined(_WIN32)
int wmain(int argc, const wchar_t *const *argv)
{
    // Keep paths and diagnostics UTF-8 internally; the Windows CRT's narrow argv uses the local code page.
    std::vector<std::string> arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            std::cerr << "Command-line argument is too long\n";
            return USAGE_ERROR_EXIT_CODE;
        }
        if (argument.empty()) {
            arguments.emplace_back();
            continue;
        }
        const auto length = static_cast<int>(argument.size());
        const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument.data(), length, nullptr, 0,
                                               nullptr, nullptr);
        if (bytes == 0) {
            std::cerr << "Invalid Unicode command-line argument\n";
            return USAGE_ERROR_EXIT_CODE;
        }
        std::string encoded(bytes, '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument.data(), length, encoded.data(), bytes,
                                nullptr, nullptr) != bytes) {
            std::cerr << "Cannot encode command-line argument\n";
            return USAGE_ERROR_EXIT_CODE;
        }
        arguments.push_back(std::move(encoded));
    }
    std::vector<const char *> pointers;
    pointers.reserve(arguments.size());
    for (const auto &argument : arguments) {
        pointers.push_back(argument.c_str());
    }
    return Run(argc, pointers.data());
}
#else
int main(int argc, const char *const *argv)
{
    return Run(argc, argv);
}
#endif
