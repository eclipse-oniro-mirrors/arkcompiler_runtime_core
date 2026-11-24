/**
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
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

#ifndef LIBPANDAFILE_SIGNAL_SAFE_DEBUG_HELPERS_H
#define LIBPANDAFILE_SIGNAL_SAFE_DEBUG_HELPERS_H

#include "file.h"
#include "line_number_program.h"
#include "libpandabase/utils/span.h"

namespace panda::panda_file::debug_helpers {

// Signal-safe line number program resolver. Collects entries into a fixed-size
// stack array, then resolves line/column/sourceFile via upper_bound search.
// No heap allocation, no locks, no LOG — safe for use in signal handlers.
class SignalSafeFrameResolver {
public:
    // Most functions have < 32 entries; 64 gives headroom with ~1KB stack usage.
    static constexpr size_t MAX_ENTRIES = 64;

    struct Entry {
        uint32_t offset;
        int32_t line;
        int32_t column;
        const char *sourceFile;
    };

    SignalSafeFrameResolver(const File *pf, File::EntityId initialFile, uint32_t lineStartRaw,
                            Span<const uint8_t> constantPool, uint32_t bcOffset)
        : bcOffset_(bcOffset),
          count_(0),
          resolvedLine_(-1),
          resolvedColumn_(-1),
          resolvedSourceFile_(nullptr),
          firstColumn_(-1),
          state_(*pf, initialFile, lineStartRaw, constantPool)
    {
    }

    LineProgramState *GetState()
    {
        return &state_;
    }

    int32_t GetLine() const
    {
        return resolvedLine_;
    }

    int32_t GetColumn() const
    {
        return resolvedColumn_;
    }

    const char *GetSourceFile() const
    {
        return resolvedSourceFile_;
    }

    void ProcessBegin()
    {
        AddEntry(state_.GetAddress(), state_.GetLine(), static_cast<int32_t>(state_.GetColumn()));
    }

    void ProcessEnd()
    {
        // Use function declaration line (first valid entry), matching
        // DebugInfoExtractor::GetFristLine/GetFristColumn semantics.
        ResolveFirstLineAndColumn();
    }

    bool HandleAdvanceLine(int32_t lineDiff)
    {
        state_.AdvanceLine(lineDiff);
        return true;
    }

    bool HandleAdvancePc(uint32_t pcDiff)
    {
        state_.AdvancePc(pcDiff);
        return true;
    }

    bool HandleSetFile(uint32_t sourceFileId)
    {
        state_.SetFile(sourceFileId);
        // Retroactively update entries_[0] sourceFile: in HAR abc files the
        // initial file (EntityId(0)) is invalid, sourceFile comes from SET_FILE.
        if (count_ > 0 && entries_[0].sourceFile == nullptr && state_.HasFile()) {
            entries_[0].sourceFile = reinterpret_cast<const char *>(state_.GetFile());
        }
        return true;
    }

    bool HandleSetSourceCode([[maybe_unused]] uint32_t sourceCodeId)
    {
        return true;
    }

    bool HandleSetPrologueEnd()
    {
        return true;
    }

    bool HandleSetEpilogueBegin()
    {
        return true;
    }

    bool HandleStartLocal([[maybe_unused]] int32_t regNumber, [[maybe_unused]] uint32_t nameId,
                          [[maybe_unused]] uint32_t typeId) const
    {
        return true;
    }

    bool HandleStartLocalExtended([[maybe_unused]] int32_t regNumber, [[maybe_unused]] uint32_t nameId,
                                  [[maybe_unused]] uint32_t typeId, [[maybe_unused]] uint32_t typeSignatureId) const
    {
        return true;
    }

    bool HandleEndLocal([[maybe_unused]] int32_t regNumber) const
    {
        return true;
    }

    bool HandleSetColumn(int32_t columnNumber)
    {
        state_.SetColumn(columnNumber);
        auto col = static_cast<int32_t>(state_.GetColumn());
        if (firstColumn_ == -1) {
            firstColumn_ = col;
        }
        AddEntry(state_.GetAddress(), state_.GetLine(), col);
        return true;
    }

    bool HandleSpecialOpcode(uint32_t pcOffset, int32_t lineOffset)
    {
        state_.AdvancePc(pcOffset);
        state_.AdvanceLine(lineOffset);
        AddEntry(state_.GetAddress(), state_.GetLine(), static_cast<int32_t>(state_.GetColumn()));
        return true;
    }

private:
    void AddEntry(uint32_t offset, int32_t line, int32_t column)
    {
        if (count_ < MAX_ENTRIES) {
            const char *sf = state_.HasFile() ? reinterpret_cast<const char *>(state_.GetFile()) : nullptr;
            entries_[count_] = {offset, line, column, sf};
            ++count_;
        }
    }

    void ResolveLine()
    {
        if (count_ == 0) {
            return;
        }
        // Linear upper_bound: find first entry with offset > bcOffset_
        size_t idx = 0;
        while (idx < count_ && entries_[idx].offset <= bcOffset_) {
            ++idx;
        }
        if (idx == 0) {
            return;
        }
        resolvedLine_ = entries_[idx - 1].line;
        resolvedColumn_ = entries_[idx - 1].column;
        resolvedSourceFile_ = entries_[idx - 1].sourceFile;
    }

    // Resolve to function declaration line/column (first valid entry).
    void ResolveFirstLineAndColumn()
    {
        if (count_ == 0) {
            return;
        }
        // Skip SPECIAL_LINE_MARK (-1) entries, matching GetFristLine semantics.
        for (size_t i = 0; i < count_; i++) {
            if (entries_[i].line != -1) {
                resolvedLine_ = entries_[i].line;
                resolvedSourceFile_ = entries_[i].sourceFile;
                break;
            }
        }
        // Use first SET_COLUMN value; ProcessBegin's column=0 is a default, not a real opcode.
        if (firstColumn_ >= 0) {
            resolvedColumn_ = firstColumn_;
        }
    }

    uint32_t bcOffset_;
    size_t count_;
    int32_t resolvedLine_;
    int32_t resolvedColumn_;
    const char *resolvedSourceFile_;
    int32_t firstColumn_;
    Entry entries_[MAX_ENTRIES];
    LineProgramState state_;
};

}  // namespace panda::panda_file::debug_helpers

#endif  // LIBPANDAFILE_SIGNAL_SAFE_DEBUG_HELPERS_H
