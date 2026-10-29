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

#include <gtest/gtest.h>
#include <elfio/elfio.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace ark::test {

class ArkInterpreterSymbolTest : public testing::Test {
public:
    void SetUp() override {}

    void TearDown() override {}
};

namespace {
// Allowed prefixes for function symbols in libarkinterpreter.so
constexpr std::string_view HANDLE_PREFIX = "HANDLE_FAST_";
constexpr std::string_view EXECUTE_PREFIX = "ExecuteImplFast";

bool IsAllowedSymbol(std::string_view name)
{
    return name.substr(0, HANDLE_PREFIX.length()) == HANDLE_PREFIX ||
           name.substr(0, EXECUTE_PREFIX.length()) == EXECUTE_PREFIX;
}

// Read dynamic symbol table from ELF file and collect function symbol names
std::vector<std::string> CollectFunctionSymbols(const std::string &path)
{
    std::vector<std::string> symbols;

    ELFIO::elfio reader;
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return symbols;
    }

    using LoadFn = bool (ELFIO::elfio::*)(std::istream &, bool) noexcept;
    LoadFn loadFn = &ELFIO::elfio::load;
    if (!(reader.*loadFn)(stream, false)) {
        std::cerr << "Failed to load ELF file: " << path << std::endl;
        return symbols;
    }

    ELFIO::section *dynsymSec = reader.sections[".dynsym"];
    if (dynsymSec == nullptr) {
        std::cerr << "Failed to find .dynsym in " << path << std::endl;
        return symbols;
    }

    ELFIO::symbol_section_accessor accessor(reader, dynsymSec);
    ELFIO::Elf_Xword symCount = accessor.get_symbols_num();

    std::string name;
    ELFIO::Elf64_Addr value;
    ELFIO::Elf_Xword size;
    unsigned char bind;
    unsigned char type;
    ELFIO::Elf_Half sectionIndex;
    unsigned char other;

    for (ELFIO::Elf_Xword i = 0; i < symCount; i++) {
        if (!accessor.get_symbol(i, name, value, size, bind, type, sectionIndex, other)) {
            continue;
        }
        if (type == ELFIO::STT_FUNC && sectionIndex != ELFIO::SHN_UNDEF && !name.empty()) {
            symbols.emplace_back(name);
        }
    }

    return symbols;
}

}  // namespace

TEST_F(ArkInterpreterSymbolTest, OnlyHandleAndExecuteSymbols)
{
    // Path to libarkinterpreter.so is passed via environment variable
    const char *soPath = std::getenv("ARKINTERPRETER_SO_PATH");
    ASSERT_NE(soPath, nullptr) << "ARKINTERPRETER_SO_PATH environment variable is not set";
    std::string path(soPath);

    auto symbols = CollectFunctionSymbols(path);
    ASSERT_FALSE(symbols.empty()) << "No function symbols found in " << path;

    std::vector<std::string> violations;
    for (const auto &sym : symbols) {
        if (!IsAllowedSymbol(sym)) {
            violations.push_back(sym);
        }
    }

    EXPECT_TRUE(violations.empty()) << "libarkinterpreter.so contains unexpected function symbols "
                                    << "(expected only HANDLE_FAST_* and ExecuteImplFast*).\n"
                                    << "Likely caused by entrypoints.cpp functions or their call "
                                    << "chain being inlined by LLVM IR inlining. Please check your "
                                    << "modifications.";
    for (const auto &v : violations) {
        std::cout << "  " << v << std::endl;
    }
}

}  // namespace ark::test
