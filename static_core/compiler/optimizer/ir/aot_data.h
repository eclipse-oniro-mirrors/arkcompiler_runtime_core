/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#ifndef PANDA_AOT_DATA_H
#define PANDA_AOT_DATA_H

#include <map>
#include <vector>
#include "libarkbase/utils/arena_containers.h"
#include "runtime_interface.h"

namespace ark::panda_file {
class File;
}  // namespace ark::panda_file

namespace ark::compiler {
class Graph;

// Registry of shared stub copies. Each entrypoint id maps to the list of
// addresses of all stub copies emitted so far; a new copy is APPENDED, never
// overwriting an existing entry, so that no address record is lost (a lost
// record would be unrecoverable for out-of-order compilation). A lookup returns
// the copy closest to the caller: bl range is symmetric (+/-128MB), so if the
// nearest copy is out of range, every copy is, and the caller falls back to
// emitting a local copy. Later out-of-order compilation phases (e.g.,
// multi-threaded code generation, layout reordering) depend on these preserved
// copy addresses for correctness, and as the information base for future
// global optimization (greedy stub placement).
class SharedStubTable {
public:
    // Append the address of a newly emitted stub copy. Old records stay.
    void AddStubOffset(RuntimeInterface::EntrypointId id, uintptr_t offset)
    {
        offsets_[static_cast<size_t>(id)].push_back(offset);
    }

    // Return the address of the stub copy closest to `caller`; 0 if no copy
    // was ever registered for this id.
    uintptr_t GetClosestStubOffset(RuntimeInterface::EntrypointId id, uintptr_t caller) const
    {
        uintptr_t best = 0;
        uintptr_t bestDist = std::numeric_limits<uintptr_t>::max();
        for (uintptr_t offset : offsets_[static_cast<size_t>(id)]) {
            uintptr_t dist = (offset > caller) ? (offset - caller) : (caller - offset);
            if (dist < bestDist) {
                bestDist = dist;
                best = offset;
            }
        }
        return best;
    }

    // Direct accessors for tests: all registered copies of one id.
    const std::vector<uintptr_t> &GetAllStubOffsets(RuntimeInterface::EntrypointId id) const
    {
        return offsets_[static_cast<size_t>(id)];
    }

private:
    static constexpr size_t SIZE = static_cast<size_t>(RuntimeInterface::EntrypointId::COUNT);
    std::array<std::vector<uintptr_t>, SIZE> offsets_;
};

// Slow-path stubs and returning-entrypoint PLT stubs have different calling
// conventions (PLT stubs use a ceremony-protected scratch register; slow-path
// stubs use a pool-temp scratch), so each flavor keeps its own SharedStubTable
// keyed by EntrypointId — sharing one table risks silent cross-flavor stub
// substitution.
class SharedSlowPathData : public SharedStubTable {};

class SharedPltData : public SharedStubTable {};

class AotData {
    static constexpr uintptr_t INVALID_ADDRESS = std::numeric_limits<uintptr_t>::max();
    using AddressType = uintptr_t;
    using File = const panda_file::File;
    using MethodPtr = RuntimeInterface::MethodPtr;
    using ClassPtr = RuntimeInterface::ClassPtr;

public:
    struct AotDataArgs {
        File *pfile;
        Graph *graph;
        AddressType codeAddr;
        uint64_t *intfInlineCacheIndex;
        std::array<std::map<std::pair<const File *, uint32_t>, std::pair<int32_t, bool>> *, 4U> mapArgs32;
        std::array<std::map<std::pair<const File *, uint64_t>, int32_t> *, 2U> mapArgs64;
    };

    explicit AotData(const AotDataArgs &args);

    // Stub-sharing registries are backend-specific: the ark encoder needs both
    // (slow-path and PLT stub reuse), while the LLVM backend emits its own
    // IR-level slow paths and never dereferences them. They are therefore not
    // part of AotDataArgs; the ark path (paoc) injects them right after
    // construction and the accessors below ASSERT they are set.
    void SetSharedSlowPathData(SharedSlowPathData *data)
    {
        slowPathData_ = data;
    }
    void SetPltData(SharedPltData *data)
    {
        pltData_ = data;
    }

    intptr_t GetEpTableOffset() const;
    intptr_t GetEntrypointOffset(uint64_t pc, int32_t slotId) const;
    intptr_t GetSharedSlowPathOffset(RuntimeInterface::EntrypointId id, uintptr_t pc) const;
    void SetSharedSlowPathOffset(RuntimeInterface::EntrypointId id, uintptr_t pc);
    intptr_t GetPltStubOffset(RuntimeInterface::EntrypointId id, uintptr_t pc) const;
    void SetPltStubOffset(RuntimeInterface::EntrypointId id, uintptr_t pc);
    intptr_t GetPltSlotOffset(uint64_t pc, uint32_t methodId, uint32_t fileIndex);
    intptr_t GetVirtIndexSlotOffset(uint64_t pc, uint32_t methodId, uint32_t fileIndex);
    intptr_t GetClassSlotOffset(uint64_t pc, uint32_t klassId, bool init, uint32_t fileIndex);
    intptr_t GetCommonSlotOffset(uint64_t pc, uint32_t id);
    intptr_t GetStringSlotOffset(uint64_t pc, uint32_t stringId, uint32_t fileIndex);
    uint64_t GetInfInlineCacheSlotOffset(uint64_t pc, uint64_t cacheIdx);

    int32_t GetClassSlotId(uint32_t klassId, uint32_t fileIndex);
    int32_t GetStringSlotId(uint32_t stringId, uint32_t fileIndex);
    int32_t GetPltSlotId(uint32_t methodId, uint32_t fileIndex);
    int32_t GetIntfInlineCacheSlotId(uint64_t cacheIdx);

    AddressType GetCodeOffset() const
    {
        return codeAddress_;
    }
    bool GetUseCha() const
    {
        return useCha_;
    }
    void SetUseCha(bool useCha)
    {
        useCha_ = useCha;
    }

    void SetIntfInlineCacheIndex(uint64_t intfInlineCacheIndex)
    {
        *intfInlineCacheIndex_ = intfInlineCacheIndex;
    }

    uint64_t GetIntfInlineCacheIndex() const
    {
        return *intfInlineCacheIndex_;
    }

    bool HasProfileData() const
    {
        return hasProfileData_;
    }

    void SetHasProfileData(bool hasProfileData)
    {
        hasProfileData_ = hasProfileData;
    }

    bool IsLLVMAotMode() const
    {
        return isLLVMAot_;
    }

    void SetIsLLVMAotMode(bool isLLVMAot)
    {
        isLLVMAot_ = isLLVMAot;
    }

private:
    inline int32_t GetSlotId() const;

    inline std::pair<const File *, bool> GetPandaFileByIndex(uint32_t fileIndex) const;
    uint32_t FindOrInsertSlotId(std::map<std::pair<const File *, uint32_t>, std::pair<int32_t, bool>> *gotTable,
                                uint32_t id, uint32_t fileIndex);

    const File *pfile_;
    Graph *graph_ {nullptr};
    SharedSlowPathData *slowPathData_ {nullptr};
    SharedPltData *pltData_ {nullptr};
    AddressType codeAddress_ {INVALID_ADDRESS};
    uint64_t *intfInlineCacheIndex_;
    std::map<std::pair<const File *, uint32_t>, std::pair<int32_t, bool>> *gotPlt_;
    std::map<std::pair<const File *, uint32_t>, std::pair<int32_t, bool>> *gotVirtIndexes_;
    std::map<std::pair<const File *, uint32_t>, std::pair<int32_t, bool>> *gotClass_;
    std::map<std::pair<const File *, uint32_t>, std::pair<int32_t, bool>> *gotString_;
    std::map<std::pair<const File *, uint64_t>, int32_t> *gotIntfInlineCache_;
    std::map<std::pair<const File *, uint64_t>, int32_t> *gotCommon_;
    bool useCha_ {false};
    bool hasProfileData_ {false};
    bool isLLVMAot_ {false};
};
}  // namespace ark::compiler

#endif  // PANDA_AOT_DATA_H
