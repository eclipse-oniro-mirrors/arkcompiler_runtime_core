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

#ifndef BYTECODE_EXPLORER_CORE_READER_DEBUG_BUILD_DETECTOR_H
#define BYTECODE_EXPLORER_CORE_READER_DEBUG_BUILD_DETECTOR_H

#include <cstdint>

namespace panda::panda_file {
class File;
}  // namespace panda::panda_file

namespace panda::bytecode_explorer {

// Debug and release builds share one ABC format: the header carries no build
// flag. A debug compilation (--debug-info) is identified by content that only
// it can produce: debug-only bytecode opcodes and debug-only line program
// opcodes. Release builds never emit any of them, so a single hit proves a
// debug build, while a complete scan without hits supports RELEASE.
enum class AbcBuildMode : uint8_t {
    // The scan did not complete (malformed input): no statement is possible.
    UNKNOWN,
    // A complete scan found no debug-only content.
    RELEASE,
    // At least one debug-only marker was found.
    DEBUG,
};

struct BuildModeEvidence {
    // debugger / newlexenvwithname / wide.newlexenvwithname instructions.
    uint64_t debugOnlyInstructions {0};
    // START_LOCAL / START_LOCAL_EXTENDED / END_LOCAL entries in line programs.
    uint64_t variableTableEntries {0};
    // SET_SOURCE_CODE entries in line programs.
    uint64_t sourceCodeMarkers {0};
    // Methods whose bytecode and debug info were walked.
    uint64_t scannedMethods {0};
    bool scanComplete {false};
};

struct BuildModeReport {
    AbcBuildMode mode {AbcBuildMode::UNKNOWN};
    BuildModeEvidence evidence;
};

// Best-effort scan over every record method. Never throws: malformed input
// yields UNKNOWN with scanComplete=false instead of failing the open.
BuildModeReport DetectAbcBuildMode(const panda_file::File &file);

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_DEBUG_BUILD_DETECTOR_H
