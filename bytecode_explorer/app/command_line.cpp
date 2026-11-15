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

#include "app/command_line.h"

#include <optional>
#include <string>
#include <string_view>

namespace panda::bytecode_explorer {
namespace {

Result<CommandLineOptions> InvalidArguments(const std::string &message)
{
    return Result<CommandLineOptions>::Failure(Status(ErrorCode::INVALID_ARGUMENT, message));
}

// True when any other argument has already been recorded: --help and --version are exclusive.
bool HasExclusiveConflict(const CommandLineOptions &options)
{
    return options.showHelp || options.showVersion || options.dumpStructure || options.showInternals ||
           options.noOpen || options.abcPath.has_value();
}

// Records one option argument (one that starts with '-'); returns the failure message when the
// option is unknown or cannot be combined with the arguments recorded so far.
std::optional<std::string> ApplyOptionArgument(std::string_view argument, CommandLineOptions *options)
{
    if (argument == "--help" || argument == "-h") {
        if (HasExclusiveConflict(*options)) {
            return "Help cannot be combined with other arguments";
        }
        options->showHelp = true;
        return std::nullopt;
    }
    if (argument == "--version") {
        if (HasExclusiveConflict(*options)) {
            return "Version cannot be combined with other arguments";
        }
        options->showVersion = true;
        return std::nullopt;
    }
    if (argument == "--no-open") {
        if (options->showHelp || options->showVersion || options->noOpen) {
            return "--no-open cannot be combined with this argument";
        }
        options->noOpen = true;
        return std::nullopt;
    }
    if (argument == "--dump-structure") {
        if (options->showHelp || options->showVersion || options->dumpStructure) {
            return "Structure dump cannot be combined with this argument";
        }
        options->dumpStructure = true;
        return std::nullopt;
    }
    if (argument == "--show-internals") {
        if (options->showHelp || options->showVersion || options->showInternals) {
            return "Internal entities cannot be combined with this argument";
        }
        options->showInternals = true;
        return std::nullopt;
    }
    return "Unknown option: " + std::string(argument);
}

// Validates the cross-option rules that involve more than one recorded option.
std::optional<std::string> ValidateOptionCombination(const CommandLineOptions &options)
{
    if (options.dumpStructure && !options.abcPath.has_value()) {
        return "--dump-structure requires an ABC path";
    }
    if (options.showInternals && !options.dumpStructure) {
        return "--show-internals requires --dump-structure";
    }
    if (options.noOpen && options.dumpStructure) {
        return "--no-open cannot be combined with --dump-structure";
    }
    return std::nullopt;
}

}  // namespace

Result<CommandLineOptions> ParseCommandLine(int argc, const char *const *argv)
{
    if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
        return InvalidArguments("Invalid process arguments");
    }

    CommandLineOptions options;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            return InvalidArguments("Invalid null command-line argument");
        }

        std::string_view argument(argv[index]);
        if (!argument.empty() && argument.front() == '-') {
            const auto error = ApplyOptionArgument(argument, &options);
            if (error.has_value()) {
                return InvalidArguments(*error);
            }
            continue;
        }
        if (options.showHelp || options.showVersion) {
            return InvalidArguments("ABC path cannot be combined with an option");
        }
        if (options.abcPath.has_value()) {
            return InvalidArguments("Only one ABC path may be specified");
        }
        options.abcPath = std::string(argument);
    }

    const auto combination = ValidateOptionCombination(options);
    if (combination.has_value()) {
        return InvalidArguments(*combination);
    }
    // A missing path is allowed: the explorer starts without a document and
    // offers an in-page picker instead.

    return Result<CommandLineOptions>::Success(std::move(options));
}

}  // namespace panda::bytecode_explorer
