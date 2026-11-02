/**
 * Copyright (c) 2023-2026 Huawei Device Co., Ltd.
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

#include "utils.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <unordered_set>
#include "class_data_accessor-inl.h"
#include "file.h"
#include "libpandafile/util/collect_util.h"
#include "utils/logger.h"

namespace panda::verifier {

void GenerateModifiedAbc(const std::vector<unsigned char> &buffer, const std::string &filename)
{
    std::ofstream abc_file(filename, std::ios::out | std::ios::binary);
    if (abc_file.fail()) {
        LOG(ERROR, VERIFIER) << "Failed to open file " << filename;
        return;
    }

    abc_file.write(reinterpret_cast<const char *>(buffer.data()), buffer.size());
    abc_file.close();
}

void ConvertToLittleEndian(std::vector<unsigned char> &inner_id, const uint32_t &id)
{
    std::vector<unsigned char> bytes;
    for (int i = 0; i < sizeof(uint32_t); ++i) {
        unsigned char byte = static_cast<unsigned char>((id >> (i * 8)) & 0xff);
        inner_id.push_back(byte);
    }
}

void ModifyBuffer(std::unordered_map<uint32_t, uint32_t> &literal_map, std::vector<unsigned char> &buffer)
{
    for (const auto &literal : literal_map) {
        size_t literal_id = literal.first;
        std::vector<unsigned char> inner_id;
        ConvertToLittleEndian(inner_id, literal.second);
        for (size_t i = literal_id; i < buffer.size(); ++i) {
            if (buffer[i] == inner_id[0] && buffer[i+1] == inner_id[1]) {
                // The purpose of this modification is to break abc
                // The abc is tampered with by setting buffer[i + 1] to buffer[i] and buffer[i + 2] to buffer[i + 1]
                buffer[i] = buffer[i + 1];
                buffer[i + 1] = buffer[i + 2];
                break;
            }
        }
    }
}

// Scans the raw bytecode buffer for the first instruction anchored on the
// defineclasswithbuffer shape and outputs the offset of its literal-array
// index operand. See FindDefineClassLiteralOperand for the anchoring rules.
static bool FindDefineClassOperandInBuffer(const std::vector<unsigned char> &buffer,
                                           Span<const panda_file::File::EntityId> method_index,
                                           const std::unordered_set<uint32_t> &method_id_offsets,
                                           const std::unordered_set<uint32_t> &literal_id_offsets,
                                           size_t &operand_offset)
{
    // Primary opcode of defineclasswithbuffer, see isa.yaml.
    constexpr uint8_t DEFINE_CLASS_WITH_BUFFER_PRIMARY_OPCODE = 0x35;
    for (size_t i = 0; i + 6U <= buffer.size(); ++i) {
        if (buffer[i] != DEFINE_CLASS_WITH_BUFFER_PRIMARY_OPCODE) {
            continue;
        }
        const uint32_t class_idx = buffer[i + 2U] | (buffer[i + 3U] << 8U);
        const uint32_t literal_idx = buffer[i + 4U] | (buffer[i + 5U] << 8U);
        if (class_idx >= method_index.size() || literal_idx >= method_index.size()) {
            continue;
        }
        if (method_id_offsets.count(method_index[class_idx].GetOffset()) == 0) {
            continue;
        }
        if (literal_id_offsets.count(method_index[literal_idx].GetOffset()) == 0) {
            continue;
        }
        operand_offset = i + 4U;
        return true;
    }
    return false;
}

bool FindDefineClassLiteralOperand(const std::string &abc_file_name,
                                   const std::vector<unsigned char> &buffer,
                                   size_t &operand_offset,
                                   uint32_t &method_cp_index)
{
    auto pf = panda_file::OpenPandaFile(abc_file_name);
    if (pf == nullptr) {
        return false;
    }
    std::unordered_set<uint32_t> method_id_offsets;
    for (const auto class_offset : pf->GetClasses()) {
        panda_file::ClassDataAccessor class_accessor(*pf, panda_file::File::EntityId(class_offset));
        class_accessor.EnumerateMethods([&method_id_offsets](panda_file::MethodDataAccessor &method_accessor) {
            method_id_offsets.insert(method_accessor.GetMethodId().GetOffset());
        });
    }
    std::unordered_set<uint32_t> literal_id_offsets;
    if (panda_file::ContainsLiteralArrayInHeader(pf->GetHeader()->version)) {
        for (const auto literal_offset : pf->GetLiteralArrays()) {
            literal_id_offsets.insert(literal_offset);
        }
    } else {
        libpandafile::CollectUtil collect_util;
        collect_util.CollectLiteralArray(*pf, literal_id_offsets);
    }
    const auto index_headers = pf->GetIndexHeaders();
    if (index_headers.empty()) {
        return false;
    }
    const auto method_index = pf->GetMethodIndex(&index_headers[0]);

    bool method_index_found = false;
    for (size_t idx = 0; idx < method_index.size(); ++idx) {
        if (method_id_offsets.count(method_index[idx].GetOffset()) > 0) {
            method_cp_index = static_cast<uint32_t>(idx);
            method_index_found = true;
            break;
        }
    }
    if (!method_index_found) {
        return false;
    }

    return FindDefineClassOperandInBuffer(buffer, method_index, method_id_offsets, literal_id_offsets, operand_offset);
}

} // namespace panda::verifier