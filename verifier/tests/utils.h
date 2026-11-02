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

#ifndef UTILS_H
#define UTILS_H

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace panda::verifier {
    void GenerateModifiedAbc(const std::vector<unsigned char> &buffer, const std::string &filename);
    void ConvertToLittleEndian(std::vector<unsigned char> &inner_literal_id, const uint32_t &id);
    void ModifyBuffer(std::unordered_map<uint32_t, uint32_t> &inner_literal_map, std::vector<unsigned char> &buffer);

    // Locates the literal-array index operand of the first `defineclasswithbuffer`
    // instruction in `buffer` together with a valid method constant-pool index,
    // without relying on hardcoded ids: the abc layout produced by the frontend
    // may shift, and a plain byte-pair scan then hits an unrelated earlier
    // occurrence. The operand is anchored on the instruction shape
    // [primary opcode 0x35][imm1][method idx][literal idx][imm2][vreg] and on
    // the constant-pool index section of the file. Returns false when the abc
    // contains no anchored instruction.
    bool FindDefineClassLiteralOperand(const std::string &abc_file_name,
                                       const std::vector<unsigned char> &buffer,
                                       size_t &operand_offset,
                                       uint32_t &method_cp_index);
} // namespace panda::verifier

#endif
