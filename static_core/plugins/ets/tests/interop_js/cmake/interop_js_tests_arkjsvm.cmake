# Copyright (c) 2025 Huawei Device Co., Ltd.
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

add_custom_target(ets_interop_js_arkjsvm_gtests COMMENT "Run ets_interop_js Gtests on ArkJSVM")
add_dependencies(ets_gtests ets_interop_js_arkjsvm_gtests)
add_dependencies(ets_interop_tests ets_interop_js_arkjsvm_gtests)

add_custom_target(ets_interop_js_tests_arkjsvm COMMENT "Run ets_interop_js tests on ArkJSVM")
add_dependencies(ets_tests ets_interop_js_tests_arkjsvm)
add_dependencies(ets_interop_tests ets_interop_js_tests_arkjsvm)

function(compile_js_file TARGET)
    cmake_parse_arguments(
        ARG
        ""
        ""
        "SOURCES;OUTPUT_DIR;OUTPUT_ABC_PATHS;COMPILE_JS_OPTION"
        ${ARGN}
    )

    if(NOT DEFINED ARG_SOURCES)
        message(FATAL_ERROR "SOURCES should be passed in compile_js_file")
    endif()

    set(BUILD_DIR)
    set(ABC_FILES)

    if(NOT DEFINED ARG_OUTPUT_DIR)
        set(BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
    else()
        set(BUILD_DIR ${ARG_OUTPUT_DIR})
    endif()

    foreach(JS_SOURCE ${ARG_SOURCES})
        set(CUR_OUTPUT_ABC)
        get_filename_component(CLEAR_NAME ${JS_SOURCE} NAME_WLE)

        # determine output path of abc file
        get_filename_component(DIR_NAME "${JS_SOURCE}" DIRECTORY)
        if (NOT ${DIR_NAME} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
            # in this case source file in in subdirectory
            string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/" "" DIR_NAME "${DIR_NAME}")
            set(CUR_OUTPUT_ABC ${BUILD_DIR}/${DIR_NAME}/${CLEAR_NAME}.abc)
        else()
            set(CUR_OUTPUT_ABC ${BUILD_DIR}/${CLEAR_NAME}.abc)
        endif()

        list(APPEND ABC_FILES ${CUR_OUTPUT_ABC})

        add_custom_command(
            OUTPUT ${CUR_OUTPUT_ABC}
            COMMENT "${TARGET}: Convert js file to ${CUR_OUTPUT_ABC}"
            COMMAND mkdir -p ${BUILD_DIR}
            COMMAND ${ES2ABC} --extension=js ${ARG_COMPILE_JS_OPTION} ${JS_SOURCE} --output=${CUR_OUTPUT_ABC}
            DEPENDS ${JS_SOURCE}
        )
    endforeach()

    set(${ARG_OUTPUT_ABC_PATHS} ${ABC_FILES} PARENT_SCOPE)

    add_custom_target(${TARGET} DEPENDS ${ABC_FILES})
endfunction(compile_js_file)

# Add Gtest-based tests to ets_interop_js_arkjsvm_gtests target.
#
# Example usage:
#   panda_ets_interop_js_arkjsvm_gtest(test_name
#     CPP_SOURCES
#       tests/unit1_test.cpp
#       tests/unit2_test.cpp
#     ETS_SOURCES
#       tests/unit1_test.sts
#       tests/unit2_test.sts
#     LIBRARIES
#       lib_target1
#       lib_target2
#     ETS_CONFIG
#       path/to/arktsconfig.json
#   )
function(panda_ets_interop_js_arkjsvm_gtest TARGET)
    # Parse arguments
    cmake_parse_arguments(
        ARG
        "COMPILATION_JS_WITH_CJS_ON"
        "ETS_CONFIG"
        "CPP_SOURCES;ETS_SOURCES;JS_SOURCES;JS_TEST_SOURCE;LIBRARIES"
        ${ARGN}
    )

    set(SO_FILES_OUTPUT "${INTEROP_TESTS_DIR}/module/lib/")

    panda_ets_interop_js_plugin(${TARGET}
        SOURCES ${ARG_CPP_SOURCES}
        LIBRARIES ets_interop_js_gtest ets_interop_js_napi_arkjsvm ${ARG_LIBRARIES}
        LIBRARY_OUTPUT_DIRECTORY ${SO_FILES_OUTPUT}
        OUTPUT_SUFFIX ".so"
    )

    set(TARGET_GTEST_PACKAGE ${TARGET}_gtest_package)
    panda_ets_package_gtest(${TARGET_GTEST_PACKAGE}
        ETS_SOURCES ${ARG_ETS_SOURCES}
        ETS_CONFIG ${ARG_ETS_CONFIG}
    )
    add_dependencies(${TARGET} ${TARGET_GTEST_PACKAGE})

    set(JS_COMPILATION_OPTIONS)
    if(DEFINED ARG_COMPILATION_JS_WITH_CJS_ON)
        set(JS_COMPILATION_OPTIONS --commonjs)
    else()
        set(JS_COMPILATION_OPTIONS --module)
    endif()

    if(DEFINED ARG_JS_SOURCES)
        compile_js_file(${TARGET}_js_modules
            SOURCES ${ARG_JS_SOURCES}
            COMPILE_JS_OPTION ${JS_COMPILATION_OPTIONS}
        )
    endif()

    # Add launcher <${TARGET}_gtests> target
    panda_ets_add_gtest(
        NAME ${TARGET}
        NO_EXECUTABLE
        NO_CORES
        CUSTOM_PRERUN_ENVIRONMENT
            "LD_LIBRARY_PATH=${PANDA_BINARY_ROOT}/lib/arkjsvm_interop/:${PANDA_BINARY_ROOT}/lib/"
            "JS_ABC_OUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}"
            "INTEROP_TEST_BUILD_DIR=${PANDA_BINARY_ROOT}/tests/ets_interop_js"
            "ARK_ETS_STDLIB_PATH=${PANDA_BINARY_ROOT}/plugins/ets/etsstdlib.abc"
            "ARK_ETS_INTEROP_JS_GTEST_ABC_PATH=${PANDA_BINARY_ROOT}/abc-gtests/${TARGET_GTEST_PACKAGE}.zip"
            "ARK_ETS_INTEROP_JS_GTEST_SOURCES=${CMAKE_CURRENT_SOURCE_DIR}"
            "ARK_ETS_INTEROP_JS_GTEST_DIR=${INTEROP_TESTS_DIR}"
            "FIXED_ISSUES=${FIXED_ISSUES}"
        LAUNCHER
            ${ARK_JS_NAPI_CLI}
            --stub-file=${ARK_JS_STUB_FILE}
            --entry-point=gtest_launcher_arkjsvm
            ${INTEROP_TESTS_DIR}/gtest_launcher_arkjsvm.abc
            ${TARGET}
        DEPS_TARGETS ${TARGET} ets_interop_js_gtest_launcher_arkjsvm
        TEST_RUN_DIR ${INTEROP_TESTS_DIR}
        OUTPUT_DIRECTORY ${INTEROP_TESTS_DIR}
    )

    if(DEFINED ARG_JS_SOURCES)
        add_dependencies(${TARGET}_gtests ${TARGET}_js_modules)
    endif()

    add_dependencies(ets_interop_js_arkjsvm_gtests ${TARGET}_gtests)
endfunction(panda_ets_interop_js_arkjsvm_gtest)

function(panda_ets_interop_js_test_arkjsvm TARGET)
    # Parse arguments
    cmake_parse_arguments(
        ARG
        ""
        "JS_LAUNCHER;ETS_CONFIG"
        "ETS_SOURCES;JS_SOURCES;ABC_FILE;LAUNCHER_ARGS;"
        ${ARGN}
    )

    if(NOT DEFINED ARG_JS_LAUNCHER)
        message("ARG_JS_LAUNCHER should be defined")
        return()
    endif()

    set(TARGET_TEST_PACKAGE ${TARGET}_test_package)
    panda_ets_package(${TARGET_TEST_PACKAGE}
        ABC_FILE ${ARG_ABC_FILE}
        ETS_SOURCES ${ARG_ETS_SOURCES}
        ETS_CONFIG ${ARG_ETS_CONFIG}
    )

    if(DEFINED ARG_JS_SOURCES)
        compile_js_file(${TARGET}_js_modules
            SOURCES ${ARG_JS_SOURCES}
            COMPILE_JS_OPTION --commonjs
        )
    endif()

    set(COMPILED_LAUNCHER_NAME ${TARGET}_launcher_abc_name)
    compile_js_file(${TARGET}_js_launcher
        SOURCES ${ARG_JS_LAUNCHER}
        OUTPUT_ABC_PATHS ${COMPILED_LAUNCHER_NAME}
        COMPILE_JS_OPTION --commonjs
    )

    # Make symbolic links to convinient work with requireNapiPreview
    set(SO_FILES_LINK_PATH "${CMAKE_CURRENT_BINARY_DIR}/module/")
    set(INTEROP_LIB_SOURCE "${PANDA_BINARY_ROOT}/lib/module/ets_interop_js_napi_arkjsvm.so")
    set(INTEROP_HELPER_LIB_SOURCE "${PANDA_BINARY_ROOT}/lib/arkjsvm_interop/libinterop_test_helper.so")

    add_custom_target(${TARGET}_create_symlinks
        COMMAND mkdir -p ${SO_FILES_LINK_PATH}
                && ln -sf ${INTEROP_LIB_SOURCE} ${INTEROP_HELPER_LIB_SOURCE} -t ${SO_FILES_LINK_PATH}
        DEPENDS ets_interop_js_napi_arkjsvm ${INTEROP_HELPER_LIB_SOURCE}
    )

    set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_interop_js_output.txt")

    set(CUSTOM_PRERUN_ENVIRONMENT
        "LD_LIBRARY_PATH=${PANDA_BINARY_ROOT}/lib/arkjsvm_interop/:${PANDA_BINARY_ROOT}/lib/"
        "ARK_ETS_INTEROP_JS_GTEST_ABC_PATH=${PANDA_BINARY_ROOT}/abc/${TARGET_TEST_PACKAGE}.zip"
        "ARK_ETS_STDLIB_PATH=${PANDA_BINARY_ROOT}/plugins/ets/etsstdlib.abc"
    )

    get_filename_component(LAUNCHER_CLEAR_NAME ${ARG_JS_LAUNCHER} NAME_WLE)

    add_custom_target(${TARGET}
        COMMAND
            ${CUSTOM_PRERUN_ENVIRONMENT}
            ${ARK_JS_NAPI_CLI}
            --stub-file=${ARK_JS_STUB_FILE}
            --entry-point=${LAUNCHER_CLEAR_NAME}
            ${${COMPILED_LAUNCHER_NAME}}
            ${ARG_LAUNCHER_ARGS}
            > ${OUTPUT_FILE} 2>&1 || (cat ${OUTPUT_FILE} && false)
        DEPENDS
            ${TARGET}_js_launcher
            ${TARGET}_js_modules
            ${TARGET}_create_symlinks
            ${TARGET_TEST_PACKAGE}
            ets_interop_js_napi_arkjsvm
    )
    add_dependencies(ets_interop_js_tests_nodevm ${TARGET})
endfunction(panda_ets_interop_js_test_arkjsvm)
