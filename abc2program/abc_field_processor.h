/**
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

#ifndef ABC2PROGRAM_ABC_FIELD_PROCESSOR_H
#define ABC2PROGRAM_ABC_FIELD_PROCESSOR_H

#include "abc_file_entity_processor.h"
#include "field_data_accessor-inl.h"
#include "common/abc_type_converter.h"

namespace panda::abc2program {

class AbcFieldProcessor : public AbcFileEntityProcessor {
public:
    AbcFieldProcessor(panda_file::File::EntityId entity_id, Abc2ProgramKeyData &key_data, pandasm::Record &record);
    void FillProgramData() override;

private:
    void FillFieldData(pandasm::Field &field);
    void FillFieldName(pandasm::Field &field);
    void FillFieldType(pandasm::Field &field);
    void FillFieldMetaData(pandasm::Field &field);
    void FillFieldAttributes(pandasm::Field &field);
    void FillFieldAnnotations(pandasm::Field &field);
    pandasm::Record &record_;
    std::unique_ptr<panda_file::FieldDataAccessor> field_data_accessor_;
    std::unique_ptr<AbcTypeConverter> type_converter_;
};

} // namespace panda::abc2program

#endif // ABC2PROGRAM_ABC_FIELD_PROCESSOR_H
