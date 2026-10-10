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

# Hot reload smoke cases: fixture pairs (a base module and its patch versions) driven by
# tests/ani/hotreload_smoke. Every case is a plain build target under `ets_tests`, so the
# whole suite runs with `ninja ets_tests` (and CI guards it like any other test):
#
#   - the fixture's own `run` asserts the semantics with std.testing (`arktest`): state
#     preservation, and either the new bodies (applied) or the untouched old ones (refused)
#   - the driver pins the reload's return code via HR_EXPECT_RC, and the entry modes with
#     HR_VIA_ENTRY / HR_BOOT_APPEND / HR_WORKERS

# Host-only: the smoke cases execute the test driver during the build, which requires a
# host-arch binary -- skip the whole suite when cross-compiling (the ARM device build).
if (CMAKE_CROSSCOMPILING)
    return()
endif()

# Per-case timeout: matches the old bash runner's MT_TIMEOUT_SECONDS. A deadlock must fail
# the build rather than hang indefinitely; overridable for slow CI machines.
if (NOT DEFINED HOTRELOAD_TEST_TIMEOUT)
    set(HOTRELOAD_TEST_TIMEOUT 600)
endif()

add_custom_target(ets_hotreload_smoke_tests)
add_dependencies(ets_tests ets_hotreload_smoke_tests)

# The fixture basename per family: es2panda derives the record prefix from the source
# basename, so every directory of one family (the base and all its patch versions) MUST
# share the same basename -- the swap matches records by that prefix.
set(HOTRELOAD_FIXTURE_STEM_basic_base greeter)
set(HOTRELOAD_FIXTURE_STEM_basic_patch greeter)
set(HOTRELOAD_STEM_greeter greeter)
set(HOTRELOAD_FIXTURE_STEM_patch_add_field greeter)
set(HOTRELOAD_FIXTURE_STEM_inc_base incremental)
set(HOTRELOAD_FIXTURE_STEM_patch_partial incremental)
set(HOTRELOAD_FIXTURE_STEM_fields_base field_storage)
set(HOTRELOAD_FIXTURE_STEM_patch_field_order field_storage)
set(HOTRELOAD_FIXTURE_STEM_patch_field_retype field_storage)
set(HOTRELOAD_FIXTURE_STEM_funcval_base funcval)
set(HOTRELOAD_FIXTURE_STEM_funcval_patch funcval)
set(HOTRELOAD_FIXTURE_STEM_inherit_base hierarchy)
set(HOTRELOAD_FIXTURE_STEM_inherit_patch hierarchy)
set(HOTRELOAD_FIXTURE_STEM_repeat_base repeated_reload)
set(HOTRELOAD_FIXTURE_STEM_repeat_patch repeated_reload)
set(HOTRELOAD_FIXTURE_STEM_workers_base workers)
set(HOTRELOAD_FIXTURE_STEM_workers_patch workers)
set(HOTRELOAD_FIXTURE_STEM_concurrent_base concurrent_load)
set(HOTRELOAD_FIXTURE_STEM_concurrent_patch concurrent_load)
set(HOTRELOAD_FIXTURE_STEM_partial_v1 generation)
set(HOTRELOAD_FIXTURE_STEM_partial_v2 generation)
set(HOTRELOAD_FIXTURE_STEM_partial_v3 generation)
set(HOTRELOAD_FIXTURE_STEM_chain_v1 chain_reload)
set(HOTRELOAD_FIXTURE_STEM_chain_v2 chain_reload)
set(HOTRELOAD_FIXTURE_STEM_chain_v3 chain_reload)
set(HOTRELOAD_FIXTURE_STEM_ambiguity_driver ambiguity_driver)

function(hotreload_add_case NAME)
    set(oneValueArgs PROGRAM PATCH SECOND_PATCH TARGET TARGET_FILE EXPECT_RC ARG PROGRAM_FILE PATCH_FILE)
    set(multiValueArgs ENV)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(fixtures ${CMAKE_CURRENT_SOURCE_DIR})
    set(out ${CMAKE_CURRENT_BINARY_DIR})

    if (DEFINED ARG_PROGRAM_FILE)
        set(program_abc ${ARG_PROGRAM_FILE})
        set(module_name ${HOTRELOAD_FIXTURE_STEM_${ARG_PROGRAM}})
    else()
        set(program_stem ${HOTRELOAD_FIXTURE_STEM_${ARG_PROGRAM}})
        set(program_src ${fixtures}/${ARG_PROGRAM}/${program_stem}.ets)
        set(program_abc ${out}/${ARG_PROGRAM}.abc)
        compile_ets_code(${program_src} ${program_abc} ${NAME}-${ARG_PROGRAM}-abc)
        set(module_name ${program_stem})
    endif()

    set(run_deps ani_hotreload_smoke etsstdlib)

    set(target_abc ${program_abc})
    if (DEFINED ARG_TARGET_FILE)
        # A package path (e.g. a .hap): the driver resolves it the way the framework would,
        # deriving the modules_static.abc form from the package path.
        set(target_abc ${ARG_TARGET_FILE})
    elseif (DEFINED ARG_TARGET)
        # The target is named by a path derived from a package; the harness takes the package
        # path and resolves it the way the framework would. The target fixture is compiled here as
        # well: without a dependency on its compilation, a parallel build can run this case
        # before the case that owns the fixture has produced the abc, and the driver would
        # race a missing file.
        set(target_abc ${out}/${ARG_TARGET}.abc)
        compile_ets_code(${fixtures}/${ARG_TARGET}/${HOTRELOAD_FIXTURE_STEM_${ARG_TARGET}}.ets
                         ${target_abc} ${NAME}-${ARG_TARGET}-abc)
        list(APPEND run_deps ${NAME}-${ARG_TARGET}-abc)
    endif()

    if (DEFINED ARG_PROGRAM AND NOT DEFINED ARG_PROGRAM_FILE)
        list(APPEND run_deps ${NAME}-${ARG_PROGRAM}-abc)
    endif()
    if (DEFINED ARG_PATCH AND NOT ARG_PATCH STREQUAL "none" AND NOT ARG_PATCH STREQUAL "none-hqf")
        list(APPEND run_deps ${NAME}-${ARG_PATCH}-abc)
    endif()

    if (DEFINED ARG_PATCH_FILE)
        set(patch_abc ${ARG_PATCH_FILE})
    elseif (ARG_PATCH STREQUAL "none")
        # A nonexistent bare patch: the open failure is the point of the case.
        set(patch_abc ${out}/does-not-exist.abc)
    elseif (ARG_PATCH STREQUAL "none-hqf")
        # A nonexistent package patch: the extractor init failure is the point of the case.
        set(patch_abc ${out}/does-not-exist.hqf)
    else()
        set(patch_src ${fixtures}/${ARG_PATCH}/${HOTRELOAD_FIXTURE_STEM_${ARG_PATCH}}.ets)
        set(patch_abc ${out}/${ARG_PATCH}.abc)
        compile_ets_code(${patch_src} ${patch_abc} ${NAME}-${ARG_PATCH}-abc)
    endif()

    set(command ${CMAKE_COMMAND} -E env)
    foreach(var IN LISTS ARG_ENV)
        list(APPEND command "${var}")
    endforeach()
    if (DEFINED ARG_EXPECT_RC)
        list(APPEND command "HR_EXPECT_RC=${ARG_EXPECT_RC}")
    endif()
    if (DEFINED ARG_SECOND_PATCH)
        set(second_src ${fixtures}/${ARG_SECOND_PATCH}/${HOTRELOAD_FIXTURE_STEM_${ARG_SECOND_PATCH}}.ets)
        set(second_abc ${out}/${ARG_SECOND_PATCH}.abc)
        compile_ets_code(${second_src} ${second_abc} ${NAME}-${ARG_SECOND_PATCH}-abc)
        list(APPEND run_deps ${NAME}-${ARG_SECOND_PATCH}-abc)
    else()
        set(second_abc "")
    endif()

    set(extra_arg "")
    if (DEFINED ARG_ARG)
        set(extra_arg ${ARG_ARG})
    endif()

    set(run_command ${command}
         timeout ${HOTRELOAD_TEST_TIMEOUT}
         $<TARGET_FILE:ani_hotreload_smoke>
         ${BOOT_PANDA_FILES}
         ${program_abc}
         ${module_name}
         ${target_abc}
         ${patch_abc}
    )
    if (NOT extra_arg STREQUAL "")
        list(APPEND run_command ${extra_arg})
    elseif (NOT second_abc STREQUAL "")
        list(APPEND run_command ${second_abc})
    endif()

    add_custom_target(${NAME}
        COMMAND ${run_command}
        DEPENDS ${run_deps}
        VERBATIM
        COMMENT "hotreload smoke ${NAME}"
    )
    add_dependencies(ets_hotreload_smoke_tests ${NAME})
endfunction()

# A1: method bodies swapped, instance and static state preserved
hotreload_add_case(hotreload_positive PROGRAM basic_base PATCH basic_patch EXPECT_RC 0)

# A7: structural change (added field) refused, nothing modified
hotreload_add_case(hotreload_add_field PROGRAM basic_base PATCH patch_add_field EXPECT_RC 17)

# A8: unreadable patch refused, nothing modified
hotreload_add_case(hotreload_no_patch PROGRAM basic_base PATCH none EXPECT_RC 22)

# A20: the entry invoked under a non-boot managed frame is refused (caller authorization)
hotreload_add_case(hotreload_caller_not_boot PROGRAM basic_base PATCH basic_patch
                   EXPECT_RC 28 ENV HR_VIA_ENTRY=1)

# A17: incremental patch -- only `Greeter` is carried; the omitted classes keep their old bodies
hotreload_add_case(hotreload_incremental PROGRAM inc_base PATCH patch_partial EXPECT_RC 0)

# A9: same-field reorder refused (offsets are assigned in declaration order)
hotreload_add_case(hotreload_field_order PROGRAM fields_base PATCH patch_field_order EXPECT_RC 17)

# reference field retyped, which the type-id check cannot see: still refused
hotreload_add_case(hotreload_field_retype PROGRAM fields_base PATCH patch_field_retype EXPECT_RC 17)

# by-name and by-value calls must agree after a reload; the identity of a pre-reload value
# is redirected to the new generation
hotreload_add_case(hotreload_fnval PROGRAM funcval_base PATCH funcval_patch EXPECT_RC 0)

# hierarchy change applied where allowed; the interface and the erroneous class are skipped
hotreload_add_case(hotreload_inheritance PROGRAM inherit_base PATCH inherit_patch EXPECT_RC 0)

# 400 alternating reloads between the patch and the base file, all must commit
hotreload_add_case(hotreload_repeat PROGRAM repeat_base PATCH repeat_patch)

# boot-context classes in the target set refuse the transaction
hotreload_add_case(hotreload_boot_ctx PROGRAM basic_base PATCH basic_patch
                   EXPECT_RC 8 ENV HR_BOOT_APPEND=1)

# two successive partial reloads driven by one stable target path
hotreload_add_case(hotreload_gen_three PROGRAM partial_v1 PATCH partial_v2 SECOND_PATCH partial_v3)

# two successive incremental reloads, each carrying only `Counter`, plus the first use of a
# class no patch contains
hotreload_add_case(hotreload_chain_inc PROGRAM chain_v1 PATCH chain_v2 SECOND_PATCH chain_v3)

# an ambiguous target (two live panda files for one path) must be refused
hotreload_add_case(hotreload_target_ambiguous PROGRAM ambiguity_driver PATCH partial_v2 TARGET partial_v1 EXPECT_RC 21)

# reloads racing real worker threads that lazily load classes: every reload commits or is
# refused with a retryable code, and no class is left on the old generation
hotreload_add_case(hotreload_concurrent_load PROGRAM concurrent_base PATCH concurrent_patch
                   ENV HR_WORKERS=2)

# A13: reload while the coroutines are spinning (their frames finish the old body)
hotreload_add_case(hotreload_mt_workers_spinning PROGRAM workers_base PATCH workers_patch
                   ENV HR_WORKERS=5 ARG wait)

# A14: reload while the coroutines exist but have not started (they enter the new body)
hotreload_add_case(hotreload_mt_workers_queued PROGRAM workers_base PATCH workers_patch
                   ENV HR_WORKERS=1 ARG nowait)

# ---------------------------------------------------------------------------
# Package-shaped cases: the patch and the target are real hqf/hap archives
# (a zip whose `ets/modules_static.abc` entry holds the abc, STORED like real
# packages, because the runtime mmaps them).
# ---------------------------------------------------------------------------

set(hotreload_pkg ${CMAKE_CURRENT_BINARY_DIR}/pkg)
set(hotreload_pkg_ets ${hotreload_pkg}/target/ets)
# The zip commands chdir into their staging trees, which must exist before the command starts.
file(MAKE_DIRECTORY ${hotreload_pkg_ets} ${hotreload_pkg}/patch-ets/ets ${hotreload_pkg}/wrong-ets/ets)

# The device shape of the target: the abc lives at the derived modules_static.abc location and
# the caller names the .hap package.
add_custom_command(
    OUTPUT ${hotreload_pkg_ets}/modules_static.abc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${hotreload_pkg_ets}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/basic_base.abc
            ${hotreload_pkg_ets}/modules_static.abc
    DEPENDS hotreload_positive-basic_base-abc ${CMAKE_CURRENT_BINARY_DIR}/basic_base.abc
)
add_custom_command(
    OUTPUT ${hotreload_pkg}/target.hap
    COMMAND zip -0 -X -q -r ${hotreload_pkg}/target.hap ets
    WORKING_DIRECTORY ${hotreload_pkg}/target
    DEPENDS ${hotreload_pkg_ets}/modules_static.abc
)
add_custom_target(hotreload_pkg_target DEPENDS ${hotreload_pkg}/target.hap)

# A genuine hqf patch: the abc at the entry the loader looks for.
add_custom_command(
    OUTPUT ${hotreload_pkg}/patch.hqf
    COMMAND ${CMAKE_COMMAND} -E make_directory ${hotreload_pkg}/patch-ets/ets
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/basic_patch.abc
            ${hotreload_pkg}/patch-ets/ets/modules_static.abc
    COMMAND zip -0 -X -q -r ${hotreload_pkg}/patch.hqf ets
    WORKING_DIRECTORY ${hotreload_pkg}/patch-ets
    DEPENDS hotreload_positive-basic_patch-abc ${CMAKE_CURRENT_BINARY_DIR}/basic_patch.abc
)
add_custom_target(hotreload_pkg_patch DEPENDS ${hotreload_pkg}/patch.hqf)

# A package holding a perfectly valid abc, but NOT at the entry the loader looks for: it must
# be refused rather than quietly opened some other way.
add_custom_command(
    OUTPUT ${hotreload_pkg}/wrong-entry.hqf
    COMMAND ${CMAKE_COMMAND} -E make_directory ${hotreload_pkg}/wrong-ets/ets
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/basic_patch.abc
            ${hotreload_pkg}/wrong-ets/ets/classes.abc
    COMMAND zip -0 -X -q -r ${hotreload_pkg}/wrong-entry.hqf classes.abc
    WORKING_DIRECTORY ${hotreload_pkg}/wrong-ets/ets
    DEPENDS hotreload_positive-basic_patch-abc ${CMAKE_CURRENT_BINARY_DIR}/basic_patch.abc
)
add_custom_target(hotreload_pkg_wrong_entry DEPENDS ${hotreload_pkg}/wrong-entry.hqf)

# A11: device shape end to end -- target named by its .hap path, patched from a .hqf
hotreload_add_case(hotreload_hqf PROGRAM basic_base
                   PROGRAM_FILE ${hotreload_pkg_ets}/modules_static.abc
                   PATCH_FILE ${hotreload_pkg}/patch.hqf
                   TARGET_FILE ${hotreload_pkg}/target.hap EXPECT_RC 0)

# A12: a package that cannot be unpacked is refused, nothing modified
hotreload_add_case(hotreload_bad_package PROGRAM basic_base PATCH none-hqf EXPECT_RC 22)

# A16: a package whose abc sits at the wrong entry is refused, NOT silently opened another way
hotreload_add_case(hotreload_wrong_entry PROGRAM basic_base
                   PATCH_FILE ${hotreload_pkg}/wrong-entry.hqf EXPECT_RC 22)

add_dependencies(hotreload_hqf hotreload_pkg_target hotreload_pkg_patch)
add_dependencies(hotreload_wrong_entry hotreload_pkg_wrong_entry)
