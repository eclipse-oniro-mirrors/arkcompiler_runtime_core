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

#include "core/reader/literal_array_reader.h"

#include "libpandafile/file-inl.h"
#include "libpandafile/helpers.h"
#include "libpandafile/literal_data_accessor.h"

#include <map>
#include <utility>

namespace panda::bytecode_explorer {
namespace {

std::string LiteralSlotContext(uint32_t literalOffset, size_t slot)
{
    return "literal array " + std::to_string(literalOffset) + " slot " + std::to_string(slot);
}

// Reports whether the tag starts an inlined element run, which the snapshot walk does not decode.
bool IsArrayElementRun(panda_file::LiteralTag tag)
{
    switch (tag) {
        case panda_file::LiteralTag::ARRAY_U1:
        case panda_file::LiteralTag::ARRAY_U8:
        case panda_file::LiteralTag::ARRAY_I8:
        case panda_file::LiteralTag::ARRAY_U16:
        case panda_file::LiteralTag::ARRAY_I16:
        case panda_file::LiteralTag::ARRAY_U32:
        case panda_file::LiteralTag::ARRAY_I32:
        case panda_file::LiteralTag::ARRAY_U64:
        case panda_file::LiteralTag::ARRAY_I64:
        case panda_file::LiteralTag::ARRAY_F32:
        case panda_file::LiteralTag::ARRAY_F64:
        case panda_file::LiteralTag::ARRAY_STRING:
            return true;
        default:
            return false;
    }
}

// Consumes the payload of one scalar slot. The INTEGER slot carries the instance method count; an
// unknown tag aborts the walk.
void ConsumeScalarSlot(panda_file::LiteralTag tag, Span<const uint8_t> *span, uint32_t *instanceMethodCount,
                       bool *hasInstanceMethodCount)
{
    switch (tag) {
        case panda_file::LiteralTag::INTEGER:
            *instanceMethodCount = panda_file::helpers::Read<sizeof(uint32_t)>(span);
            *hasInstanceMethodCount = true;
            break;
        case panda_file::LiteralTag::LITERALBUFFERINDEX:
        case panda_file::LiteralTag::FLOAT:
        case panda_file::LiteralTag::ETS_IMPLEMENTS:
            panda_file::helpers::Read<sizeof(uint32_t)>(span);
            break;
        case panda_file::LiteralTag::DOUBLE:
            panda_file::helpers::Read<sizeof(uint64_t)>(span);
            break;
        case panda_file::LiteralTag::BOOL:
        case panda_file::LiteralTag::TAGVALUE:
        case panda_file::LiteralTag::BUILTINTYPEINDEX:
        case panda_file::LiteralTag::ACCESSOR:
        case panda_file::LiteralTag::NULLVALUE:
            panda_file::helpers::Read<sizeof(uint8_t)>(span);
            break;
        case panda_file::LiteralTag::METHODAFFILIATE:
            panda_file::helpers::Read<sizeof(uint16_t)>(span);
            break;
        default:
            throw panda_file::helpers::FileAccessException("Invalid literal tag");
    }
}

// Records the metadata reference of one string slot.
void AppendStringSlot(PhysicalLiteralArray *physical, uint32_t literalOffset, size_t slot, uint32_t stringOffset)
{
    physical->metadataReferences.push_back(
        {PhysicalMetadataTargetKind::STRING, PhysicalMetadataReferenceKind::LITERAL_STRING, stringOffset,
         physical->range, std::nullopt, static_cast<uint32_t>(slot), LiteralSlotContext(literalOffset, slot)});
}

// Records the nested array offset and its metadata reference for one nested array slot.
void AppendNestedArraySlot(PhysicalLiteralArray *physical, uint32_t literalOffset, size_t slot, uint32_t nestedOffset)
{
    physical->nestedArrayOffsets.push_back(nestedOffset);
    physical->metadataReferences.push_back(
        {PhysicalMetadataTargetKind::LITERAL_ARRAY, PhysicalMetadataReferenceKind::NESTED_LITERAL_ARRAY, nestedOffset,
         physical->range, std::nullopt, static_cast<uint32_t>(slot), LiteralSlotContext(literalOffset, slot)});
}

// Records the method entry and metadata reference of one method-typed slot.
void AppendMethodSlot(PhysicalLiteralArray *physical, uint32_t literalOffset, size_t slot, uint32_t methodOffset,
                      PhysicalLiteralMethodKind kind)
{
    physical->methods.push_back({methodOffset, {}, kind, false});
    physical->metadataReferences.push_back(
        {PhysicalMetadataTargetKind::METHOD, PhysicalMetadataReferenceKind::LITERAL_METHOD, methodOffset,
         physical->range, std::nullopt, static_cast<uint32_t>(slot), LiteralSlotContext(literalOffset, slot)});
}

// State of one literal-array walk: the moving span cursor, the output array with its file offset
// and the instance-method counter with its presence flag.
struct LiteralArrayWalk {
    Span<const uint8_t> *span {nullptr};
    PhysicalLiteralArray *physical {nullptr};
    uint32_t literalOffset {0U};
    uint32_t *instanceMethodCount {nullptr};
    bool *hasInstanceMethodCount {nullptr};
};

// Decodes one literal slot: entity slots append their methods and references, scalar slots only
// consume their payload, and unknown tags abort the walk.
void ApplyLiteralSlot(panda_file::LiteralTag tag, size_t slot, LiteralArrayWalk *walk)
{
    switch (tag) {
        case panda_file::LiteralTag::STRING:
            AppendStringSlot(walk->physical, walk->literalOffset, slot,
                             panda_file::helpers::Read<sizeof(uint32_t)>(walk->span));
            return;
        case panda_file::LiteralTag::LITERALARRAY:
            AppendNestedArraySlot(walk->physical, walk->literalOffset, slot,
                                  panda_file::helpers::Read<sizeof(uint32_t)>(walk->span));
            return;
        case panda_file::LiteralTag::METHOD:
        case panda_file::LiteralTag::GENERATORMETHOD:
        case panda_file::LiteralTag::ASYNCGENERATORMETHOD:
            AppendMethodSlot(walk->physical, walk->literalOffset, slot,
                             panda_file::helpers::Read<sizeof(uint32_t)>(walk->span),
                             PhysicalLiteralMethodKind::METHOD);
            return;
        case panda_file::LiteralTag::GETTER:
            AppendMethodSlot(walk->physical, walk->literalOffset, slot,
                             panda_file::helpers::Read<sizeof(uint32_t)>(walk->span),
                             PhysicalLiteralMethodKind::GETTER);
            return;
        case panda_file::LiteralTag::SETTER:
            AppendMethodSlot(walk->physical, walk->literalOffset, slot,
                             panda_file::helpers::Read<sizeof(uint32_t)>(walk->span),
                             PhysicalLiteralMethodKind::SETTER);
            return;
        default:
            ConsumeScalarSlot(tag, walk->span, walk->instanceMethodCount, walk->hasInstanceMethodCount);
    }
}

PhysicalLiteralArray ReadOne(const panda_file::File &file, uint32_t offset, bool snapshotReferences)
{
    const auto id = panda_file::File::EntityId(offset);
    auto span = file.GetSpanFromId(id);
    const auto valueCount = panda_file::helpers::Read<panda_file::ID_SIZE>(&span);
    PhysicalLiteralArray physical {FileRange {offset, 0U}, valueCount, {}, {}, {}};
    if (!snapshotReferences) {
        return physical;
    }

    uint32_t instanceMethodCount = 0U;
    bool hasInstanceMethodCount = false;
    LiteralArrayWalk walk {&span, &physical, offset, &instanceMethodCount, &hasInstanceMethodCount};
    size_t index = 0;
    while (index < valueCount) {
        const auto tag = static_cast<panda_file::LiteralTag>(panda_file::helpers::Read<panda_file::TAG_SIZE>(&span));
        if (IsArrayElementRun(tag)) {
            index = valueCount;
        } else {
            ApplyLiteralSlot(tag, index, &walk);
        }
        index += 2U;
    }
    if (hasInstanceMethodCount) {
        for (size_t methodIndex = instanceMethodCount; methodIndex < physical.methods.size(); ++methodIndex) {
            physical.methods[methodIndex].isStatic = true;
        }
    }
    return physical;
}

}  // namespace

std::vector<PhysicalLiteralArray> LiteralArrayReader::Read(const panda_file::File &file, std::set<uint32_t> inventory,
                                                           std::set<uint32_t> traversable,
                                                           const std::set<uint32_t> &excludedOffsets)
{
    std::map<uint32_t, PhysicalLiteralArray> snapshots;
    for (const auto offset : inventory) {
        if (excludedOffsets.find(offset) == excludedOffsets.end()) {
            traversable.emplace(offset);
        }
    }
    while (!traversable.empty()) {
        const auto offset = *traversable.begin();
        traversable.erase(traversable.begin());
        if (snapshots.find(offset) != snapshots.end() || excludedOffsets.find(offset) != excludedOffsets.end()) {
            continue;
        }
        auto literal = ReadOne(file, offset, true);
        for (const auto nestedOffset : literal.nestedArrayOffsets) {
            inventory.emplace(nestedOffset);
            traversable.emplace(nestedOffset);
        }
        snapshots.emplace(offset, std::move(literal));
    }

    std::vector<PhysicalLiteralArray> result;
    result.reserve(inventory.size());
    for (const auto offset : inventory) {
        const auto snapshot = snapshots.find(offset);
        result.push_back(snapshot == snapshots.end() ? ReadOne(file, offset, false) : std::move(snapshot->second));
    }
    return result;
}

}  // namespace panda::bytecode_explorer
