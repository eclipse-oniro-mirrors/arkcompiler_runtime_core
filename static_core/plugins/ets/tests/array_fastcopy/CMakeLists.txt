# Copyright (c) 2026 Huawei Device Co., Ltd.
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

set(ARRAY_FASTCOPY_TEST_LIST
    "overlap_copy"
)

add_custom_target(ets_tests_array_fastcopy)
add_dependencies(ets_tests ets_tests_array_fastcopy)

# NOTE: the variants below assemble the test without `--optimize` on purpose:
# ark_asm --optimize asserts in AsmEmitter::AddBytecodeIndexDependencies when
# the re-emitted program contains a call.range to an externally declared
# method, and this test must use call.range for the five-argument copy
# intrinsics. Assembler peepholes are irrelevant to what is verified here
# (fast path correctness under JIT/AOT), so plain assembly is used instead.

foreach(NAME ${ARRAY_FASTCOPY_TEST_LIST})
    panda_add_asm_file(
        FILE ${PANDA_ETS_PLUGIN_SOURCE}/tests/array_fastcopy/${NAME}.pa
        TARGET ets_array_fastcopy_${NAME}_cpp
        DEPENDS etsstdlib
        LANGUAGE_CONTEXT ets
        ADDITIONAL_STDLIBS $<TARGET_PROPERTY:etsstdlib,FILE>
        RUNTIME_OPTIONS "--interpreter-type=cpp"
    )
    add_dependencies(ets_tests_array_fastcopy ets_array_fastcopy_${NAME}_cpp)

    panda_add_asm_file(
        FILE ${PANDA_ETS_PLUGIN_SOURCE}/tests/array_fastcopy/${NAME}.pa
        TARGET ets_array_fastcopy_${NAME}_irtoc
        DEPENDS etsstdlib
        LANGUAGE_CONTEXT ets
        ADDITIONAL_STDLIBS $<TARGET_PROPERTY:etsstdlib,FILE>
        RUNTIME_OPTIONS "--interpreter-type=irtoc"
    )
    add_dependencies(ets_tests_array_fastcopy ets_array_fastcopy_${NAME}_irtoc)

    if(PANDA_COMPILER_ENABLE AND NOT PANDA_TARGET_ARM32)

        # 26982 64-bit pointer support
        if (PANDA_32_BIT_MANAGED_POINTER OR NOT PANDA_TARGET_ARM64)
            panda_add_asm_file(
                FILE ${PANDA_ETS_PLUGIN_SOURCE}/tests/array_fastcopy/${NAME}.pa
                TARGET ets_array_fastcopy_${NAME}_enforce_jit
                DEPENDS etsstdlib
                LANGUAGE_CONTEXT ets
                ADDITIONAL_STDLIBS $<TARGET_PROPERTY:etsstdlib,FILE>
                RUNTIME_OPTIONS "--compiler-enable-jit=true" "--compiler-hotness-threshold=0" "--no-async-jit"
            )
            add_dependencies(ets_tests_array_fastcopy ets_array_fastcopy_${NAME}_enforce_jit)
        endif()

        if (PANDA_TARGET_ARM64 OR PANDA_TARGET_AMD64)
            panda_add_asm_file(
                FILE ${PANDA_ETS_PLUGIN_SOURCE}/tests/array_fastcopy/${NAME}.pa
                TARGET ets_array_fastcopy_${NAME}_aot
                DEPENDS etsstdlib
                LANGUAGE_CONTEXT ets
                AOT_MODE TRUE
                ADDITIONAL_STDLIBS $<TARGET_PROPERTY:etsstdlib,FILE>
                RUNTIME_OPTIONS "--compiler-enable-jit=true"
            )
            add_dependencies(ets_tests_array_fastcopy ets_array_fastcopy_${NAME}_aot)
        endif()
    endif()
endforeach()
