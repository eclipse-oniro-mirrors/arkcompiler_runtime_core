# Copyright (c) 2021-2024 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

project(aot_builder)

set(AOT_BUILDER_SOURCES aot_builder.cpp)

if (PANDA_LLVM_AOT)
    list(APPEND AOT_BUILDER_SOURCES llvm_aot_builder.cpp)
endif()

panda_add_library(aot_builder STATIC ${AOT_BUILDER_SOURCES})

panda_target_link_libraries(aot_builder arkbase arkfile arkcompiler)
# Compression backend for the mini debug ELF in `.gnu_debugdata`: xz via the
# 7-Zip SDK sources (third_party_lzma), compiled into the tool. The stream is
# the standard .xz format expected by gdb/eu-unstrip and by faultloggerd on
# device. Set PANDA_LZMA_SDK_ROOT to a
# third_party_lzma checkout to enable; source compilation (not a host shared
# library) keeps product and cross builds working. When it is not set the
# compression option is compiled out and behaves as a documented no-op: full
# symbols are generated and a warning is printed. The zlib fallback was
# removed on purpose: a zlib stream in .gnu_debugdata is not readable by the
# standard tooling, so "no compression" is strictly better than an ambiguous
# format.
set(PANDA_LZMA_SDK_ROOT "" CACHE PATH "Path to third_party_lzma (7-Zip SDK) containing the C/ directory")
if (PANDA_LZMA_SDK_ROOT AND NOT EXISTS "${PANDA_LZMA_SDK_ROOT}/C/7zCrc.h")
    message(WARNING "aot_builder: PANDA_LZMA_SDK_ROOT does not point to a third_party_lzma checkout "
                    "(${PANDA_LZMA_SDK_ROOT}); ignoring it")
    set(PANDA_LZMA_SDK_ROOT "")
endif()
if (PANDA_LZMA_SDK_ROOT)
    set(LZMA_SDK_DIR ${PANDA_LZMA_SDK_ROOT}/C)
    set(LZMA_SDK_SOURCES 7zStream.c Xz.c XzDec.c XzEnc.c Lzma2Enc.c Lzma2Dec.c LzmaEnc.c LzmaDec.c LzFind.c
        LzFindOpt.c LzFindMt.c MtCoder.c MtDec.c Threads.c Alloc.c 7zCrc.c 7zCrcOpt.c CpuArch.c Sort.c Bra.c
        Bra86.c BraIA64.c Bcj2.c Delta.c XzCrc64.c XzCrc64Opt.c Sha256.c Sha256Opt.c)
    panda_add_library(lzma_sdk STATIC)
    # The archive is linked transitively into the shared llvmbackend library;
    # enable PIC on the archive itself (the aot_builder-wide PIC property set
    # below does not apply to static dependencies).
    set_target_properties(lzma_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
    foreach (lzma_src IN LISTS LZMA_SDK_SOURCES)
        panda_target_sources(lzma_sdk PRIVATE ${LZMA_SDK_DIR}/${lzma_src})
    endforeach ()
    panda_target_include_directories(lzma_sdk PUBLIC ${LZMA_SDK_DIR} ${PANDA_LZMA_SDK_ROOT})
    # Third-party C sources: suppress warnings only, inherit build-type flags.
    panda_target_compile_options(lzma_sdk PRIVATE -w)
    find_package(Threads REQUIRED)
    panda_target_link_libraries(lzma_sdk PUBLIC Threads::Threads)
    panda_target_link_libraries(aot_builder lzma_sdk)
    panda_target_compile_definitions(aot_builder PUBLIC PANDA_AOT_HAS_XZSDK=1)
    panda_target_sources(aot_builder PRIVATE symbol_compression.cpp)
    message(STATUS "aot_builder: using 7-Zip SDK (third_party_lzma) for .gnu_debugdata: ${PANDA_LZMA_SDK_ROOT}")
else()
    # Only the third_party_lzma (7-Zip SDK) backend is supported: point
    # PANDA_LZMA_SDK_ROOT at a third_party_lzma checkout to enable it. Without
    # it the compression option is a documented no-op (full symbols, no
    # .gnu_debugdata section).
    message(WARNING "aot_builder: PANDA_LZMA_SDK_ROOT is not set; "
                    "--paoc-compress-symbols will be a no-op (full symbols, no .gnu_debugdata section)")
endif()
set_target_properties(aot_builder PROPERTIES POSITION_INDEPENDENT_CODE ON)

panda_set_lib_32bit_property(aot_builder)
