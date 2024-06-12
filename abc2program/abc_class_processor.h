/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef ABC2PROGRAM_ABC_CLASS_PROCESSOR_H
#define ABC2PROGRAM_ABC_CLASS_PROCESSOR_H

#include "abc_file_entity_processor.h"
#include "class_data_accessor-inl.h"

namespace panda::abc2program {

class AbcClassProcessor : public AbcFileEntityProcessor {
public:
    AbcClassProcessor(panda_file::File::EntityId entity_id, Abc2ProgramEntityContainer &entity_container);
    void FillProgramData() override;

private:
    void FillRecord();
    void FillRecordData();
    void FillRecordName();
    void FillRecordSourceFile();
    void FillRecordMetaData();
    void FillRecordAttributes();
    void FillRecordAnnotations();
    void FillFields();
    void FillFunctions();
    std::unique_ptr<panda_file::ClassDataAccessor> class_data_accessor_;
    pandasm::Record record_;
};

}  // namespace panda::abc2program

#endif  // ABC2PROGRAM_ABC_CLASS_PROCESSOR_H
