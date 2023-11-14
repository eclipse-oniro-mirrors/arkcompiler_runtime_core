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

#include "aot/aot_builder/aot_builder.h"
#include "aot/aot_manager.h"
#include "assembly-parser.h"
#include "unit_test.h"
#include "os/exec.h"
#include "os/filesystem.h"
#include "runtime/include/file_manager.h"

namespace panda::compiler {
class ClassHashTableTest : public AsmTest {
public:
    ClassHashTableTest()
    {
        std::string exe_path = GetExecPath();
        auto pos = exe_path.rfind('/');
        paoc_path_ = exe_path.substr(0U, pos) + "/../bin/ark_aot";
        aotdump_path_ = exe_path.substr(0U, pos) + "/../bin/ark_aotdump";
        pandastdlib_path_ = GetPaocDirectory() + "/../pandastdlib/arkstdlib.abc";
    }

    const char *GetPaocFile() const
    {
        return paoc_path_.c_str();
    }

    const char *GetAotdumpFile() const
    {
        return aotdump_path_.c_str();
    }

    const char *GetPandaStdLibFile() const
    {
        return pandastdlib_path_.c_str();
    }

    std::string GetPaocDirectory() const
    {
        auto pos = paoc_path_.rfind('/');
        return paoc_path_.substr(0U, pos);
    }

    std::string GetSourceCode() const
    {
        return source_;
    }

private:
    std::string paoc_path_;
    std::string aotdump_path_;
    std::string pandastdlib_path_;
    const std::string source_ = R"(
        .record A {}
        .record B {}
        .record C {}
        .record D {}
        .record E {}

        .function i32 A.f() {
            ldai 1
            return
        }

        .function i32 B.f() {
            ldai 2
            return
        }

        .function i32 C.f() {
            ldai 3
            return
        }

        .function i32 D.f() {
            ldai 4
            return
        }

        .function i32 main() {
            ldai 0
            return
        }
    )";
};

TEST_F(ClassHashTableTest, AddClassHashTable)
{
    TmpFile panda_fname("AddClassHashTableTest.abc");

    {
        pandasm::Parser parser;
        auto res = parser.Parse(GetSourceCode());
        ASSERT_TRUE(res);
        ASSERT_TRUE(pandasm::AsmEmitter::Emit(panda_fname.GetFileName(), res.Value()));
    }

    auto panda_file_ptr = panda_file::OpenPandaFile(panda_fname.GetFileName());
    ASSERT(panda_file_ptr != nullptr);
    auto &pfile_ref = *panda_file_ptr;

    AotBuilder aot_builder;
    aot_builder.AddClassHashTable(pfile_ref);

    auto entity_pair_headers = aot_builder.GetEntityPairHeaders();
    auto class_hash_tables_size = aot_builder.GetClassHashTableSize()->back();

    ASSERT(!entity_pair_headers->empty());
    ASSERT_EQ(class_hash_tables_size, entity_pair_headers->size());
}

TEST_F(ClassHashTableTest, GetClassHashTable)
{
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP();
    }

    TmpFile aot_fname("TestTwo.an");
    TmpFile panda_fname1("Animal.abc");
    TmpFile panda_fname2("Cat.abc");

    {
        auto source = R"(
            .record Animal {}
            .function i32 Animal.fun() <static> {
                ldai 1
                return
            }
       )";

        pandasm::Parser parser;
        auto res = parser.Parse(source);
        ASSERT_TRUE(res);
        ASSERT_TRUE(pandasm::AsmEmitter::Emit(panda_fname1.GetFileName(), res.Value()));
    }

    {
        auto source = R"(
            .record Animal <external>
            .record Cat {}
            .record Dog {}
            .record Pig {}
            .function i32 Animal.fun() <external, static>
            .function i32 Cat.main() <static> {
                call.short Animal.fun
                return
            }
       )";

        pandasm::Parser parser;
        auto res = parser.Parse(source);
        ASSERT_TRUE(res);
        ASSERT_TRUE(pandasm::AsmEmitter::Emit(panda_fname2.GetFileName(), res.Value()));
    }

    {
        auto res = os::exec::Exec(GetPaocFile(), "--paoc-panda-files", panda_fname2.GetFileName(), "--panda-files",
                                  panda_fname1.GetFileName(), "--paoc-output", aot_fname.GetFileName(),
                                  "--boot-panda-files", GetPandaStdLibFile());
        ASSERT_TRUE(res);
        ASSERT_EQ(res.Value(), 0U);
    }

    std::string filename = os::GetAbsolutePath(aot_fname.GetFileName());
    auto aot_file_ret = AotFile::Open(filename, 0U, true);
    ASSERT(aot_file_ret.Value() != nullptr);
    auto aot_file = std::move(aot_file_ret.Value());

    for (size_t i = 0; i < aot_file->GetFilesCount(); i++) {
        auto file_header = aot_file->FileHeaders()[i];
        auto table = aot_file->GetClassHashTable(file_header);

        ASSERT(!table.empty());
        ASSERT_EQ(table.size(), file_header.class_hash_table_size);
    }

    for (size_t i = 0; i < aot_file->GetFilesCount(); i++) {
        auto file_header = aot_file->FileHeaders()[i];
        AotPandaFile aot_panda_file(aot_file.get(), &file_header);
        auto table = aot_panda_file.GetClassHashTable();

        ASSERT(!table.empty());
        ASSERT_EQ(table.size(), file_header.class_hash_table_size);
    }
}

TEST_F(ClassHashTableTest, DumpClassHashTable)
{
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP();
    }

    TmpFile panda_fname("DumpClassHashTableTest.abc");
    TmpFile aot_fname("DumpClassHashTableTest.an");

    {
        pandasm::Parser parser;
        auto res = parser.Parse(GetSourceCode());
        ASSERT_TRUE(res);
        ASSERT_TRUE(pandasm::AsmEmitter::Emit(panda_fname.GetFileName(), res.Value()));
    }

    {
        auto res = os::exec::Exec(GetPaocFile(), "--paoc-panda-files", panda_fname.GetFileName(), "--paoc-output",
                                  aot_fname.GetFileName(), "--boot-panda-files", GetPandaStdLibFile());
        ASSERT_TRUE(res);
        ASSERT_EQ(res.Value(), 0U);
    }

    {
        std::string filename = os::GetAbsolutePath(aot_fname.GetFileName());
        auto res = os::exec::Exec(GetAotdumpFile(), "--show-code=disasm", filename.c_str());
        ASSERT_TRUE(res);
        ASSERT_EQ(res.Value(), 0U);
    }
}

TEST_F(ClassHashTableTest, LoadClassHashTableFromAnFileToAbcFile)
{
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP();
    }

    TmpFile panda_fname("LoadClassHashTableFromAnFileToAbcFileTest.abc");
    TmpFile aot_fname("LoadClassHashTableFromAnFileToAbcFileTest.an");

    {
        pandasm::Parser parser;
        auto res = parser.Parse(GetSourceCode());
        ASSERT_TRUE(res);
        ASSERT_TRUE(pandasm::AsmEmitter::Emit(panda_fname.GetFileName(), res.Value()));
    }

    auto pfile = panda_file::OpenPandaFile(panda_fname.GetFileName());

    {
        auto res = os::exec::Exec(GetPaocFile(), "--paoc-panda-files", panda_fname.GetFileName(), "--paoc-output",
                                  aot_fname.GetFileName(), "--boot-panda-files", GetPandaStdLibFile());
        ASSERT_TRUE(res);
        ASSERT_EQ(res.Value(), 0U);
    }

    std::string filename = os::GetAbsolutePath(aot_fname.GetFileName());
    auto aot_file_ret = AotFile::Open(filename, 0U, true);
    ASSERT(aot_file_ret.Value() != nullptr);
    auto aot_file = std::move(aot_file_ret.Value());

    auto file_header = aot_file->FileHeaders()[0U];
    AotPandaFile aot_panda_file(aot_file.get(), &file_header);
    pfile->SetClassHashTable(aot_panda_file.GetClassHashTable());

    ASSERT(!pfile->GetClassHashTable().empty());
    ASSERT_EQ(pfile->GetClassHashTable().size(), aot_panda_file.GetClassHashTable().size());
}

TEST_F(ClassHashTableTest, LoadAbcFileCanLoadClassHashTable)
{
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP();
    }

    TmpFile panda_fname("LoadAbcFileTest.abc");
    TmpFile aot_fname("LoadAbcFileTest.an");

    {
        pandasm::Parser parser;
        auto res = parser.Parse(GetSourceCode());
        ASSERT_TRUE(res);
        ASSERT_TRUE(pandasm::AsmEmitter::Emit(panda_fname.GetFileName(), res.Value()));
    }

    auto pfile = panda_file::OpenPandaFile(panda_fname.GetFileName());

    {
        auto res = os::exec::Exec(GetPaocFile(), "--gc-type=epsilon", "--paoc-panda-files", panda_fname.GetFileName(),
                                  "--paoc-output", aot_fname.GetFileName(), "--boot-panda-files", GetPandaStdLibFile());
        ASSERT_TRUE(res);
        ASSERT_EQ(res.Value(), 0U);
    }

    std::string filename = os::GetAbsolutePath(panda_fname.GetFileName());
    auto res = FileManager::LoadAbcFile(filename, panda_file::File::READ_ONLY);
    ASSERT_TRUE(res);

    const panda_file::File *pf_ptr = nullptr;
    Runtime::GetCurrent()->GetClassLinker()->EnumeratePandaFiles([&pf_ptr, filename](const panda_file::File &pf) {
        if (pf.GetFilename() == filename) {
            pf_ptr = &pf;
            return false;
        }
        return true;
    });

    ASSERT(!pf_ptr->GetClassHashTable().empty());
}

TEST_F(ClassHashTableTest, GetClassIdFromClassHashTable)
{
    if (RUNTIME_ARCH != Arch::X86_64) {
        GTEST_SKIP();
    }

    TmpFile panda_fname("GetClassIdFromClassHashTableTest.abc");
    TmpFile aot_fname("GetClassIdFromClassHashTableTest.an");

    {
        pandasm::Parser parser;
        auto res = parser.Parse(GetSourceCode());
        ASSERT_TRUE(res);
        ASSERT_TRUE(pandasm::AsmEmitter::Emit(panda_fname.GetFileName(), res.Value()));
    }

    auto pfile = panda_file::OpenPandaFile(panda_fname.GetFileName());

    auto *descriptor_a = reinterpret_cast<const uint8_t *>("A");
    auto *descriptor_b = reinterpret_cast<const uint8_t *>("B");
    auto *descriptor_c = reinterpret_cast<const uint8_t *>("C");
    auto *descriptor_d = reinterpret_cast<const uint8_t *>("D");
    auto *descriptor_e = reinterpret_cast<const uint8_t *>("E");

    auto class_id1_a = pfile->GetClassId(descriptor_a);
    auto class_id1_b = pfile->GetClassId(descriptor_b);
    auto class_id1_c = pfile->GetClassId(descriptor_c);
    auto class_id1_d = pfile->GetClassId(descriptor_d);
    auto class_id1_e = pfile->GetClassId(descriptor_e);

    {
        auto res = os::exec::Exec(GetPaocFile(), "--gc-type=epsilon", "--paoc-panda-files", panda_fname.GetFileName(),
                                  "--paoc-output", aot_fname.GetFileName(), "--boot-panda-files", GetPandaStdLibFile());
        ASSERT_TRUE(res);
        ASSERT_EQ(res.Value(), 0U);
    }

    std::string filename = os::GetAbsolutePath(panda_fname.GetFileName());
    auto res = FileManager::LoadAbcFile(filename, panda_file::File::READ_ONLY);
    ASSERT_TRUE(res);

    const panda_file::File *pf_ptr = nullptr;
    Runtime::GetCurrent()->GetClassLinker()->EnumeratePandaFiles([&pf_ptr, filename](const panda_file::File &pf) {
        if (pf.GetFilename() == filename) {
            pf_ptr = &pf;
            return false;
        }
        return true;
    });

    auto class_id2_a = pf_ptr->GetClassIdFromClassHashTable(descriptor_a);
    auto class_id2_b = pf_ptr->GetClassIdFromClassHashTable(descriptor_b);
    auto class_id2_c = pf_ptr->GetClassIdFromClassHashTable(descriptor_c);
    auto class_id2_d = pf_ptr->GetClassIdFromClassHashTable(descriptor_d);
    auto class_id2_e = pf_ptr->GetClassIdFromClassHashTable(descriptor_e);

    ASSERT_EQ(class_id1_a, class_id2_a);
    ASSERT_EQ(class_id1_b, class_id2_b);
    ASSERT_EQ(class_id1_c, class_id2_c);
    ASSERT_EQ(class_id1_d, class_id2_d);
    ASSERT_EQ(class_id1_e, class_id2_e);
}

}  // namespace panda::compiler
