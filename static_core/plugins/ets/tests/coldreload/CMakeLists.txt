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

# Cold reload smoke cases: a base module whose classes are loaded from the patch after the
# prepend, driven by tests/ani/coldreload_app. Every case is a plain build target under
# `ets_tests`, so the whole suite runs with `ninja ets_tests` (and CI guards it like any
# other test):
#
#   - the fixture's own `observe` asserts the post-reload state with std.testing (`arktest`);
#     the per-case expectations live in per-case base variants, which share identical class
#     bodies and differ only in what `observe` asserts
#   - the driver pins the reload's return code via COLDRELOAD_EXPECT_RC / _RC2

# Host-only: the smoke cases execute the test driver during the build, which requires a
# host-arch binary -- skip the whole suite when cross-compiling (the ARM device build).
if (CMAKE_CROSSCOMPILING)
    return()
endif()

add_custom_target(ets_coldreload_smoke_tests)
add_dependencies(ets_tests ets_coldreload_smoke_tests)

# The fixture basename per family: es2panda derives the record prefix from the source
# basename, so every directory of one family (the base and its patch) MUST share it.
set(COLDRELOAD_FIXTURE_STEM_base greeter)
set(COLDRELOAD_FIXTURE_STEM_full_base greeter)
set(COLDRELOAD_FIXTURE_STEM_full_v2 greeter)
set(COLDRELOAD_FIXTURE_STEM_partial greeter)
set(COLDRELOAD_FIXTURE_STEM_partial_base greeter)
set(COLDRELOAD_FIXTURE_STEM_addfield addfield)
set(COLDRELOAD_FIXTURE_STEM_addfield_base addfield)
set(COLDRELOAD_FIXTURE_STEM_addmethod addmethod)
set(COLDRELOAD_FIXTURE_STEM_addmethod_base addmethod)
set(COLDRELOAD_FIXTURE_STEM_rebase rebase)
set(COLDRELOAD_FIXTURE_STEM_rebase_base rebase)
set(COLDRELOAD_FIXTURE_STEM_twice twice)
set(COLDRELOAD_FIXTURE_STEM_twice_base twice)
set(COLDRELOAD_FIXTURE_STEM_twice_a twice)
set(COLDRELOAD_FIXTURE_STEM_twice_b twice)

function(coldreload_add_case NAME)
    set(oneValueArgs PROGRAM PATCH PATCH_FILE RELATIVE_PATCH SECOND_PATCH EXPECT_RC EXPECT_RC2 MODE ENV)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(fixtures ${CMAKE_CURRENT_SOURCE_DIR})
    set(out ${CMAKE_CURRENT_BINARY_DIR})

    set(program_stem ${COLDRELOAD_FIXTURE_STEM_${ARG_PROGRAM}})
    set(program_abc ${out}/${ARG_PROGRAM}.abc)
    compile_ets_code(${fixtures}/${ARG_PROGRAM}/${program_stem}.ets ${program_abc} ${NAME}-${ARG_PROGRAM}-abc)

    set(run_deps ${NAME}-${ARG_PROGRAM}-abc ani_coldreload_app etsstdlib)

    set(patch_arg "")
    if (DEFINED ARG_PATCH_FILE)
        set(patch_arg ${ARG_PATCH_FILE})
    elseif (ARG_PATCH STREQUAL "empty")
        # An empty patch path: the argument check is the point of the case. The cmake command
        # list cannot carry an empty element, the driver maps "-" back to "".
        set(patch_arg "-")
    elseif (ARG_PATCH STREQUAL "missing")
        set(patch_arg ${out}/does-not-exist.abc)
    elseif (ARG_PATCH STREQUAL "missing-hqf")
        set(patch_arg ${out}/does-not-exist.hqf)
    elseif (ARG_PATCH)
        set(patch_arg ${out}/${ARG_PATCH}.abc)
        compile_ets_code(${fixtures}/${ARG_PATCH}/${COLDRELOAD_FIXTURE_STEM_${ARG_PATCH}}.ets ${patch_arg} ${NAME}-${ARG_PATCH}-abc)
        list(APPEND run_deps ${NAME}-${ARG_PATCH}-abc)
    endif()

    set(patch2_arg "-")
    if (DEFINED ARG_SECOND_PATCH)
        set(patch2_arg ${out}/${ARG_SECOND_PATCH}.abc)
        compile_ets_code(${fixtures}/${ARG_SECOND_PATCH}/${COLDRELOAD_FIXTURE_STEM_${ARG_SECOND_PATCH}}.ets ${patch2_arg} ${NAME}-${ARG_SECOND_PATCH}-abc)
        list(APPEND run_deps ${NAME}-${ARG_SECOND_PATCH}-abc)
    endif()

    # The driver's mode argument is mandatory; anything but "pre" means no pre-phase.
    set(mode_arg "run")
    if (DEFINED ARG_MODE)
        set(mode_arg ${ARG_MODE})
    endif()
    set(workdir ${CMAKE_CURRENT_BINARY_DIR})
    if (DEFINED ARG_RELATIVE_PATCH)
        # The hsp entry lookup works on the derived path of the patch path, which must stay
        # relative to the driver's working directory (the binary root).
        set(patch_arg ${ARG_RELATIVE_PATCH})
        set(workdir ${PANDA_BINARY_ROOT})
    endif()

    set(command ${CMAKE_COMMAND} -E env)
    if (DEFINED ARG_EXPECT_RC)
        list(APPEND command "COLDRELOAD_EXPECT_RC=${ARG_EXPECT_RC}")
    endif()
    if (DEFINED ARG_EXPECT_RC2)
        list(APPEND command "COLDRELOAD_EXPECT_RC2=${ARG_EXPECT_RC2}")
    endif()
    if (DEFINED ARG_ENV)
        list(APPEND command "${ARG_ENV}")
    endif()
    list(APPEND command
         $<TARGET_FILE:ani_coldreload_app>
         ${BOOT_PANDA_FILES}
         ${program_abc}
         ${program_stem}
         "${patch_arg}"
    )

    # patch2 and mode are always passed (possibly empty): the driver's argv layout is fixed.
    add_custom_target(${NAME}
        COMMAND ${command} "${patch2_arg}" "${mode_arg}"
        WORKING_DIRECTORY ${workdir}
        DEPENDS ${run_deps}
        VERBATIM
        COMMENT "coldreload smoke ${NAME}"
    )
    add_dependencies(ets_coldreload_smoke_tests ${NAME})
endfunction()

# The patch and the target packages: a zip whose `ets/modules_static.abc` entry holds the abc
# (STORED, because the runtime mmaps the entries). One archive per suffix exercises the
# per-suffix readers.
set(coldreload_pkg ${CMAKE_CURRENT_BINARY_DIR}/pkg)
file(MAKE_DIRECTORY ${coldreload_pkg}/stage/ets)

add_custom_command(
    OUTPUT ${coldreload_pkg}/patch.hqf ${coldreload_pkg}/patch.hap ${coldreload_pkg}/patch.hsp
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/full_v2.abc
            ${coldreload_pkg}/stage/ets/modules_static.abc
    COMMAND zip -0 -X -q -r ${coldreload_pkg}/patch.hqf ets
    COMMAND zip -0 -X -q -r ${coldreload_pkg}/patch.hap ets
    COMMAND zip -0 -X -q -r ${coldreload_pkg}/patch.hsp ets
    WORKING_DIRECTORY ${coldreload_pkg}/stage
    DEPENDS coldreload_full-full_v2-abc ${CMAKE_CURRENT_BINARY_DIR}/full_v2.abc
)
add_custom_target(coldreload_pkg_patches
    DEPENDS ${coldreload_pkg}/patch.hqf ${coldreload_pkg}/patch.hap ${coldreload_pkg}/patch.hsp)

# A full patch: every class comes from the prepended file
coldreload_add_case(coldreload_full PROGRAM full_base PATCH full_v2 EXPECT_RC 0)

# The same, through each package shape
coldreload_add_case(coldreload_hqf PROGRAM full_base PATCH_FILE ${coldreload_pkg}/patch.hqf
                    EXPECT_RC 0)
add_dependencies(coldreload_hqf coldreload_pkg_patches)
coldreload_add_case(coldreload_hap_patch PROGRAM full_base PATCH_FILE ${coldreload_pkg}/patch.hap
                    EXPECT_RC 0)
add_dependencies(coldreload_hap_patch coldreload_pkg_patches)
# The hsp reader looks the entry up by the DERIVED path of the (relative) patch path, so the
# archive carries the entry under that derived name and the driver runs from the binary root.
set(coldreload_hsp_root plugins/ets/tests/coldreload/pkg/patch-hsp)
set(coldreload_hsp_stage ${coldreload_pkg}/hsp-stage/${coldreload_hsp_root})
file(MAKE_DIRECTORY ${coldreload_hsp_stage}/ets)
add_custom_command(
    OUTPUT ${coldreload_pkg}/patch-hsp.hsp
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/full_v2.abc
            ${coldreload_hsp_stage}/ets/modules_static.abc
    COMMAND zip -0 -X -q -r ${coldreload_pkg}/patch-hsp.hsp plugins
    WORKING_DIRECTORY ${coldreload_pkg}/hsp-stage
    DEPENDS coldreload_full-full_v2-abc ${CMAKE_CURRENT_BINARY_DIR}/full_v2.abc
)
add_custom_target(coldreload_pkg_hsp DEPENDS ${coldreload_pkg}/patch-hsp.hsp)
coldreload_add_case(coldreload_hsp_patch PROGRAM full_base
                    RELATIVE_PATCH ${coldreload_hsp_root}.hsp EXPECT_RC 0)
add_dependencies(coldreload_hsp_patch coldreload_pkg_hsp)

# A package without a readable entry is refused, not opened another way
coldreload_add_case(coldreload_bad_package PROGRAM base PATCH missing-hqf EXPECT_RC 2)

# A partial patch: the omitted classes keep loading from the original file
coldreload_add_case(coldreload_partial PROGRAM partial_base PATCH partial EXPECT_RC 0)

# Structural changes are legal in cold reload (no live state to preserve)
coldreload_add_case(coldreload_addfield PROGRAM addfield_base PATCH addfield EXPECT_RC 0)
coldreload_add_case(coldreload_addmethod PROGRAM addmethod_base PATCH addmethod EXPECT_RC 0)
coldreload_add_case(coldreload_rebase PROGRAM rebase_base PATCH rebase EXPECT_RC 0)

# Two prepends stack, newest first
coldreload_add_case(coldreload_twice PROGRAM twice_base PATCH twice_a SECOND_PATCH twice_b
                    EXPECT_RC 0 EXPECT_RC2 0)

# An empty patch path is refused by the argument check
coldreload_add_case(coldreload_empty_path PROGRAM base PATCH empty EXPECT_RC 1)

# A nonexistent patch path is refused by the open
coldreload_add_case(coldreload_bad_path PROGRAM base PATCH missing EXPECT_RC 2)

# The startup contract: once the receiver's context has loaded classes, the prepend is refused
coldreload_add_case(coldreload_preloaded PROGRAM base PATCH full_v2 EXPECT_RC 4 MODE pre)

# An AOT file for the target: compiled code may embed the old classes, the prepend is refused
set(coldreload_an ${CMAKE_CURRENT_BINARY_DIR}/base.an)
coldreload_add_case(coldreload_aot PROGRAM base PATCH partial EXPECT_RC 3
                    ENV COLDRELOAD_AOT_FILE=${coldreload_an})
add_custom_command(
    OUTPUT ${coldreload_an}
    COMMAND $<TARGET_FILE:ark_aot> --paoc-panda-files=${CMAKE_CURRENT_BINARY_DIR}/base.abc
            --paoc-output=${coldreload_an}
            --paoc-location=${CMAKE_CURRENT_BINARY_DIR}
            --boot-panda-files=${BOOT_PANDA_FILES}
            --load-runtimes=ets
            --compiler-enable-jit=false
    DEPENDS coldreload_aot-base-abc ark_aot ${CMAKE_CURRENT_BINARY_DIR}/base.abc
)
add_custom_target(coldreload_target_an DEPENDS ${coldreload_an})
add_dependencies(coldreload_aot coldreload_target_an)
set_target_properties(coldreload_aot PROPERTIES
    ADDITIONAL_CLEAN_FILES ${coldreload_an})

# A native-only patch (no abc entry in the package) is a no-op success: cold reload only
# reorders class lookup, the .so files are the framework's business.
set(coldreload_native_stage ${coldreload_pkg}/native-stage/libs/arm64-v8a)
file(MAKE_DIRECTORY ${coldreload_native_stage})
add_custom_command(
    OUTPUT ${coldreload_pkg}/native-only.hqf
    COMMAND ${CMAKE_COMMAND} -E touch ${coldreload_native_stage}/libdummy.so
    COMMAND zip -0 -X -q -r ${coldreload_pkg}/native-only.hqf libs
    WORKING_DIRECTORY ${coldreload_pkg}/native-stage
)
add_custom_target(coldreload_pkg_native_only DEPENDS ${coldreload_pkg}/native-only.hqf)
coldreload_add_case(coldreload_native_only PROGRAM base
                    PATCH_FILE ${coldreload_pkg}/native-only.hqf EXPECT_RC 0)
add_dependencies(coldreload_native_only coldreload_pkg_native_only)
