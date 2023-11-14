/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef DISASM_LIB_H_INCLUDED
#define DISASM_LIB_H_INCLUDED

#include "macros.h"
#include "utils/type_helpers.h"
#include "utils/span.h"

#include "class_data_accessor-inl.h"
#include "code_data_accessor-inl.h"
#include "debug_data_accessor-inl.h"
#include "debug_info_extractor.h"
#include "field_data_accessor-inl.h"
#include "method_data_accessor-inl.h"
#include "literal_data_accessor-inl.h"
#include "param_annotations_data_accessor.h"
#include "annotation_data_accessor.h"
#include "proto_data_accessor-inl.h"
#include "file-inl.h"
#include "file.h"
#include "os/file.h"

#include "runtime/profiling/profiling-disasm-inl.h"

#include "assembly-program.h"
#include "assembly-ins.h"

#include "bytecode_instruction-inl.h"
#include "libpandabase/macros.h"

#include <map>
#include <memory>
#include <string>

#include "accumulators.h"

namespace panda::disasm {
class Disassembler {
public:
    NO_COPY_SEMANTIC(Disassembler);
    DEFAULT_MOVE_SEMANTIC(Disassembler);

    Disassembler() = default;
    ~Disassembler()
    {
        profiling::DestroyProfile(profile_, file_language_);
    }

    PANDA_PUBLIC_API void Serialize(std::ostream &os, bool add_separators = false,
                                    bool print_information = false) const;
    PANDA_PUBLIC_API void Disassemble(std::string_view filename_in, bool quiet = false, bool skip_strings = false);
    PANDA_PUBLIC_API void Disassemble(const panda_file::File &file, bool quiet = false, bool skip_strings = false);
    PANDA_PUBLIC_API void Disassemble(std::unique_ptr<const panda_file::File> &file, bool quiet = false,
                                      bool skip_strings = false);
    PANDA_PUBLIC_API void CollectInfo();

    PANDA_PUBLIC_API void SetProfile(std::string_view fname);

    PANDA_PUBLIC_API void SetFile(std::unique_ptr<const panda_file::File> &file);
    PANDA_PUBLIC_API void SetFile(const panda_file::File &file);

    PANDA_PUBLIC_API void Serialize(const pandasm::Function &method, std::ostream &os, bool print_information = false,
                                    panda_file::LineNumberTable *line_table = nullptr) const;
    PANDA_PUBLIC_API void GetMethod(pandasm::Function *method, const panda_file::File::EntityId &method_id);

    const ProgInfo &GetProgInfo() const
    {
        return prog_info_;
    }

private:
    void DisassembleImpl(bool quiet = false, bool skip_strings = false);
    static inline bool IsSystemType(const std::string &type_name);

    void GetRecord(pandasm::Record *record, const panda_file::File::EntityId &record_id);
    void AddMethodToTables(const panda_file::File::EntityId &method_id);
    void GetLiteralArray(pandasm::LiteralArray *lit_array, size_t index);
    template <typename T>
    void FillLiteralArrayData(pandasm::LiteralArray *lit_array, const panda_file::LiteralTag &tag,
                              const panda_file::LiteralDataAccessor::LiteralValue &value) const;
    void FillLiteralData(pandasm::LiteralArray *lit_array, const panda_file::LiteralDataAccessor::LiteralValue &value,
                         const panda_file::LiteralTag &tag) const;

    static inline bool IsPandasmFriendly(char c);
    void GetLiteralArrays();
    void GetRecords();
    void GetFields(pandasm::Record *record, const panda_file::File::EntityId &record_id);

    void GetField(pandasm::Field &field, const panda_file::FieldDataAccessor &field_accessor);
    void AddExternalFieldsToRecords();

    void GetMethods(const panda_file::File::EntityId &record_id);
    void GetParams(pandasm::Function *method, const panda_file::File::EntityId &proto_id) const;
    IdList GetInstructions(pandasm::Function *method, panda_file::File::EntityId method_id,
                           panda_file::File::EntityId code_id);
    LabelTable GetExceptions(pandasm::Function *method, panda_file::File::EntityId method_id,
                             panda_file::File::EntityId code_id) const;
    bool LocateTryBlock(const BytecodeInstruction &bc_ins, const BytecodeInstruction &bc_ins_last,
                        const panda_file::CodeDataAccessor::TryBlock &try_block,
                        pandasm::Function::CatchBlock *catch_block_pa, LabelTable *label_table, size_t try_idx) const;
    bool LocateCatchBlock(const BytecodeInstruction &bc_ins, const BytecodeInstruction &bc_ins_last,
                          const panda_file::CodeDataAccessor::CatchBlock &catch_block,
                          pandasm::Function::CatchBlock *catch_block_pa, LabelTable *label_table, size_t try_idx,
                          size_t catch_idx) const;

    void GetMetaData(pandasm::Record *record, const panda_file::File::EntityId &record_id) const;
    void GetMetaData(pandasm::Function *method, const panda_file::File::EntityId &method_id) const;
    void GetMetaData(pandasm::Field *field, const panda_file::File::EntityId &field_id) const;

    void GetLanguageSpecificMetadata();

    std::string AnnotationTagToString(char tag) const;

    std::string ScalarValueToString(const panda_file::ScalarValue &value, const std::string &type);
    std::string ArrayValueToString(const panda_file::ArrayValue &value, const std::string &type, size_t idx);

    std::string GetFullMethodName(const panda_file::File::EntityId &method_id) const;
    std::string GetMethodSignature(const panda_file::File::EntityId &method_id) const;
    std::string GetFullRecordName(const panda_file::File::EntityId &class_id) const;

    void GetRecordInfo(const panda_file::File::EntityId &record_id, RecordInfo *record_info) const;
    void GetMethodInfo(const panda_file::File::EntityId &method_id, MethodInfo *method_info) const;
    void GetInsInfo(panda_file::MethodDataAccessor &mda, const panda_file::File::EntityId &code_id,
                    MethodInfo *method_info) const;
    template <typename T>
    void SerializeValue(bool is_signed, const pandasm::LiteralArray::Literal &lit, std::ostream &os) const;
    void Serialize(const std::string &name, const pandasm::LiteralArray &lit_array, std::ostream &os) const;
    void SerializeValues(const pandasm::LiteralArray &lit_array, bool is_const, std::ostream &os) const;
    std::string LiteralTagToString(const panda_file::LiteralTag &tag) const;
    std::string LiteralValueToString(const pandasm::LiteralArray::Literal &lit) const;
    void Serialize(const pandasm::Record &record, std::ostream &os, bool print_information = false) const;
    void SerializeFields(const pandasm::Record &record, std::ostream &os, bool print_information,
                         bool is_union = false) const;
    void SerializeUnionFields(const pandasm::Record &record, std::ostream &os, bool print_information) const;
    void Serialize(const pandasm::Function::CatchBlock &catch_block, std::ostream &os) const;
    void Serialize(const pandasm::ItemMetadata &meta, const AnnotationList &ann_list, std::ostream &os) const;
    void SerializeLineNumberTable(const panda_file::LineNumberTable &line_number_table, std::ostream &os) const;
    void SerializeLocalVariableTable(const panda_file::LocalVariableTable &local_variable_table,
                                     const pandasm::Function &method, std::ostream &os) const;
    void SerializeLanguage(std::ostream &os) const;
    void SerializeFilename(std::ostream &os) const;
    void SerializeLitArrays(std::ostream &os, bool add_separators = false) const;
    void SerializeRecords(std::ostream &os, bool add_separators = false, bool print_information = false) const;
    void SerializeMethods(std::ostream &os, bool add_separators = false, bool print_information = false) const;

    pandasm::Type PFTypeToPandasmType(const panda_file::Type &type, panda_file::ProtoDataAccessor &pda,
                                      size_t &ref_idx) const;

    pandasm::Type FieldTypeToPandasmType(const uint32_t &type) const;

    static inline std::string StringDataToString(panda_file::File::StringData sd)
    {
        std::string str = std::string(utf::Mutf8AsCString(sd.data));
        size_t sym_pos = 0;
        while (sym_pos = str.find_first_of("\a\b\f\n\r\t\v\'\?\\", sym_pos), sym_pos != std::string::npos) {
            std::string sym;
            switch (str[sym_pos]) {
                case '\a':
                    sym = R"(\a)";
                    break;
                case '\b':
                    sym = R"(\b)";
                    break;
                case '\f':
                    sym = R"(\f)";
                    break;
                case '\n':
                    sym = R"(\n)";
                    break;
                case '\r':
                    sym = R"(\r)";
                    break;
                case '\t':
                    sym = R"(\t)";
                    break;
                case '\v':
                    sym = R"(\v)";
                    break;
                case '\'':
                    sym = R"(\')";
                    break;
                case '\?':
                    sym = R"(\?)";
                    break;
                case '\\':
                    sym = R"(\\)";
                    break;
                default:
                    UNREACHABLE();
            }
            str = str.replace(sym_pos, 1, sym);
            ASSERT(sym.size() == 2);
            sym_pos += 2;
        }
        return str;
    }

    template <typename T>
    std::string LiteralIntegralValueToString(const pandasm::LiteralArray::Literal &lit) const
    {
        std::stringstream res {};
        T value = std::get<T>(lit.value);
        if (lit.IsSigned()) {
            res << +static_cast<typename std::make_signed<T>::type>(value);
        } else {
            res << +value;
        }
        return res.str();
    }

    pandasm::Opcode BytecodeOpcodeToPandasmOpcode(BytecodeInstruction::Opcode o) const;
    pandasm::Opcode BytecodeOpcodeToPandasmOpcode(uint8_t o) const;

    pandasm::Ins BytecodeInstructionToPandasmInstruction(BytecodeInstruction bc_ins,
                                                         panda_file::File::EntityId method_id) const;

    std::string IDToString(BytecodeInstruction bc_ins, panda_file::File::EntityId method_id) const;

    panda::panda_file::SourceLang GetRecordLanguage(panda_file::File::EntityId class_id) const;

    std::unique_ptr<const panda_file::File> file_holder_ {nullptr};
    const panda_file::File *file_ {};
    pandasm::Program prog_ {};

    panda::panda_file::SourceLang file_language_ = panda::panda_file::SourceLang::PANDA_ASSEMBLY;

    std::map<std::string, panda_file::File::EntityId> record_name_to_id_ {};
    std::map<std::string, panda_file::File::EntityId> method_name_to_id_ {};

    std::map<std::string, std::vector<pandasm::Field>> external_field_table_ {};

    ProgAnnotations prog_ann_ {};

    ProgInfo prog_info_ {};

    profiling::ProfileContainer profile_ {profiling::INVALID_PROFILE};

    std::unique_ptr<panda_file::DebugInfoExtractor> debug_info_extractor_ {nullptr};

    bool quiet_ {false};
    bool skip_strings_ {false};

#include "disasm_plugins.inc"
};
}  // namespace panda::disasm

#endif
