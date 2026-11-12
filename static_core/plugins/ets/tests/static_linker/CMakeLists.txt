# Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

panda_ets_add_gtest(
    NAME static_linker_tests_ets
    SOURCES
        linker_test.cpp
    LIBRARIES
        arklinker arkruntime arkassembler
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/assembler"
    SANITIZERS
        ${PANDA_SANITIZERS_LIST}
    TEST_RUN_DIR
        "${CMAKE_CURRENT_BINARY_DIR}"
    DEPS_TARGETS
        arkstdlib
)

file(COPY data DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

if (TARGET es2panda AND TARGET etsstdlib AND NOT CMAKE_CROSSCOMPILING)
    add_definitions(-DTEST_STATIC_LINKER_WITH_STS)
    set(STS_TESTS_DIR ${CMAKE_CURRENT_BINARY_DIR}/data/ets)
    set(STS_TESTS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/data/output)

    set(STS_TESTS
        annotation
        classcall_doublefile
        classcall_multifile
        fclass
        filesinfo
        fmethod
        fmethod_overloaded
        fmethod_overloaded_2
        hello_world
        mix
        singlefile
        sys
    )
    set(STS_TESTS_OUTPUT_SUBDIRS "")
    foreach(STS_TEST ${STS_TESTS})
        set(STS_TESTS_OUTPUT_SUBDIR "${STS_TESTS_OUTPUT_DIR}/${STS_TEST}")
        if(EXISTS "${STS_TESTS_OUTPUT_SUBDIR}")
            file(REMOVE_RECURSE "${STS_TESTS_OUTPUT_SUBDIR}")
        endif()
        file(MAKE_DIRECTORY "${STS_TESTS_OUTPUT_SUBDIR}")
        list(APPEND STS_TESTS_OUTPUT_SUBDIRS "${STS_TESTS_OUTPUT_SUBDIR}")
    endforeach()

    set(STS_TESTS_SOURCES
        annotation/field.ats
        annotation/method.ats
        classcall_doublefile/bedependent.ats
        classcall_doublefile/dependency.ats
        classcall_multifile/dependency.ats
        classcall_multifile/main_dependencyUser.ats
        classcall_multifile/product_user.ats
        classcall_multifile/user_dependency.ats
        fclass/1.ats
        fclass/2.ats
        filesinfo/1.ats
        filesinfo/dir1/11.ats
        filesinfo/dir1/dir11/111.ats
        filesinfo/dir2/21.ats
        filesinfo/dir2/22.ats
        fmethod/1.ats
        fmethod/2.ats
        fmethod_overloaded/1.ats
        fmethod_overloaded/2.ats
        fmethod_overloaded_2/1.ats
        fmethod_overloaded_2/2.ats
        fmethod_overloaded_2/3.ats
        fmethod_overloaded_2/4.ats
        hello_world/1.ats
        mix/1.ats
        mix/2.ats
        singlefile/classExtension.ats
        singlefile/interImpl.ats
        singlefile/ObjectLiteral.ats
        singlefile/teserror.ats
        sys/1.ats
        sys/2.ats
    )
    set(STS_TESTS_TARGETS "")
    foreach(STS_SOURCE ${STS_TESTS_SOURCES})
        set(ABC_FILE "${STS_TESTS_DIR}/${STS_SOURCE}.abc")
        add_custom_command(
            OUTPUT "${ABC_FILE}"
            COMMAND $<TARGET_FILE:es2panda> --output "${ABC_FILE}"
                --ets-module --extension=ets "${STS_TESTS_DIR}/${STS_SOURCE}"
            DEPENDS es2panda "${STS_TESTS_DIR}/${STS_SOURCE}"
        )
        list(APPEND STS_TESTS_TARGETS "${ABC_FILE}")
    endforeach()

    # test filesinfo
    add_custom_command(
        OUTPUT "${STS_TESTS_DIR}/filesinfo/filesinfo.txt"
        COMMAND python3 "${STS_TESTS_DIR}/filesinfo/generate_filesinfo.py"
            --dir "${STS_TESTS_DIR}/filesinfo" --output "${STS_TESTS_DIR}/filesinfo/filesinfo.txt"
        DEPENDS "${STS_TESTS_TARGETS}" "${STS_TESTS_DIR}/filesinfo/generate_filesinfo.py"
    )
    list(APPEND STS_TESTS_TARGETS "${STS_TESTS_DIR}/filesinfo/filesinfo.txt")

    add_custom_target(static_linker_tests_sts
        DEPENDS "${STS_TESTS_TARGETS}" "${STS_TESTS_OUTPUT_SUBDIRS}"
    )

    # internal issue #15674
    add_custom_target(static_linker_tests_field_value
        COMMAND $<TARGET_FILE:es2panda> --output "${CMAKE_CURRENT_BINARY_DIR}/a.abc" --extension=ets "${CMAKE_CURRENT_SOURCE_DIR}/data/multi/field_value/a.ats"
        COMMAND $<TARGET_FILE:es2panda> --output "${CMAKE_CURRENT_BINARY_DIR}/b.abc" --extension=ets "${CMAKE_CURRENT_SOURCE_DIR}/data/multi/field_value/b.ats"
        COMMAND $<TARGET_FILE:ark_link> --output "${CMAKE_CURRENT_BINARY_DIR}/l.abc" -- "${CMAKE_CURRENT_BINARY_DIR}/a.abc" "${CMAKE_CURRENT_BINARY_DIR}/b.abc"
        COMMAND $<TARGET_FILE:ark> --load-runtimes ets --boot-panda-files $<TARGET_PROPERTY:etsstdlib,FILE> "${CMAKE_CURRENT_BINARY_DIR}/l.abc" b.ETSGLOBAL::main
        DEPENDS  es2panda ark ark_link etsstdlib "${CMAKE_CURRENT_SOURCE_DIR}/data/multi/field_value/a.ats" "${CMAKE_CURRENT_SOURCE_DIR}/data/multi/field_value/b.ats"
    )

    # internal issue #17770
    add_custom_target(static_linker_tests_debug_info
        COMMAND $<TARGET_FILE:es2panda> --output "${CMAKE_CURRENT_BINARY_DIR}/CustomInt.abc"
            --ets-module --extension=ets --debug-info "${CMAKE_CURRENT_SOURCE_DIR}/data/multi/dedup_debug_info/CustomInt.ats"
        COMMAND $<TARGET_FILE:es2panda> --output "${CMAKE_CURRENT_BINARY_DIR}/CustomLong.abc"
            --ets-module --extension=ets --debug-info "${CMAKE_CURRENT_SOURCE_DIR}/data/multi/dedup_debug_info/CustomLong.ats"
        COMMAND $<TARGET_FILE:es2panda> --output "${CMAKE_CURRENT_BINARY_DIR}/data/ets/classcall_doublefile/dependency_debug.abc"
            --ets-module --extension=ets --debug-info "${CMAKE_CURRENT_SOURCE_DIR}/data/ets/classcall_doublefile/dependency_debug.ats"
        COMMAND $<TARGET_FILE:ark_link> --output "${CMAKE_CURRENT_BINARY_DIR}/CustomIntLongLinked.abc"
            -- "${CMAKE_CURRENT_BINARY_DIR}/CustomInt.abc" "${CMAKE_CURRENT_BINARY_DIR}/CustomLong.abc"
        COMMAND $<TARGET_FILE:ark> --load-runtimes ets --boot-panda-files $<TARGET_PROPERTY:etsstdlib,FILE>
            "${CMAKE_CURRENT_BINARY_DIR}/CustomIntLongLinked.abc" CustomLong.CustomLong::main   # Test execution
        COMMAND $<TARGET_FILE:ark_disasm> --verbose "${CMAKE_CURRENT_BINARY_DIR}/CustomIntLongLinked.abc"
            "${CMAKE_CURRENT_BINARY_DIR}/CustomIntLongLinked.disasm.pa"                         # Test disasm with debug info reading
        DEPENDS es2panda ark ark_disasm ark_link etsstdlib
            "${CMAKE_CURRENT_SOURCE_DIR}/data/multi/dedup_debug_info/CustomInt.ats"
            "${CMAKE_CURRENT_SOURCE_DIR}/data/multi/dedup_debug_info/CustomLong.ats"
    )
    add_dependencies(static_linker_tests_ets static_linker_tests_sts static_linker_tests_field_value static_linker_tests_debug_info)
endif()
