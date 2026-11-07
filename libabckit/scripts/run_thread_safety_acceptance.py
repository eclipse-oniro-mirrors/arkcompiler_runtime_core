#!/usr/bin/env python3
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
"""Run the Dynamic thread-safety acceptance matrix and archive its provenance."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


CORE = (
    "LibAbcKitDynamicThreadSafetyTest.*:LibAbcKitGraphIsaIntegrationTest.*:"
    "LibAbcKitDynamicGraphVersionTest.*:LibAbcKitIrInterfaceTest.*:*Parallel*:AbckitScenarioHarnessTest.*"
    ":AbckitBranchEliminatorFailureTest.*"
)
NORMAL = (
    CORE + ":LibAbcKitStringStaticTest.*:LibAbcKitStringDynamicTest.*:LibAbcKitArrayStaticTest.*"
    ":LibAbcKitArkTSInspectApiOpcodesStaticTest.*:AbckitScenarioCTestClean.*:AbckitScenarioCppTestClean.*"
    ":LibAbcKitJSModifyApiModulesTest.*:LibAbcKitModifyApiAnnotationsTests.*"
    ":LibAbcKitArkTSModifyApiAnnotationsTest.*:LibAbcKitLoggerTest.*:LoggerTest.*"
    ":LibAbcKitDynamicCatchPhiTest.*:LibAbcKitStaticCatchPhiTest.*"
    ":LibAbcKitNullptrTestsInspectApiImpl*.*:LibAbcKitNullptrTestsArktsInspectApiImpl0.*"
    ":LibAbcKitNullptrTestsJsInspectApiImpl0.*:LibAbcKitNullptrTestsApiImpl0.*"
    ":LibAbcKitNullptrTestsModifyApiImpl0.*:LibAbcKitWrongCtxTestsModifyApiImpl0.*"
    ":LibAbcKitWrongCtxTestsArktsModifyApiImpl0.*"
)
TSAN = (
    CORE + "-LibAbcKitDynamicThreadSafetyTest."
    "DynamicPreparedFunctionPersistsIsaModificationAfterGraphDestroyedExecution"
    ":LibAbcKitDynamicScenarioParallelTest.*"
    ":LibAbcKitGraphIsaIntegrationTest.DynamicGraphIsaSingleThreadRegression"
)
SCENARIOS = {
    "LibAbcKitTestParallelApiScannerDynamic", "LibAbcKitTestParallelScanSubclassesDynamic",
    "LibAbcKitTestParallelAddLogDynamic", "LibAbcKitTestParallelAddTryCatchDynamic",
    "LibAbcKitTestParallelBranchEliminatorDynamic", "LibAbcKitTestParallelParameterCheckDynamic",
    "LibAbcKitTestDynamicParallelReplaceCallSite", "LibAbcKitTestParallelRouterTableDynamic",
}
ROUND_CASES = {"LibAbcKitTestParallelApiScannerDynamic", "LibAbcKitTestParallelAddLogDynamic"}
REQUIRED_CORE = {
    "RejectNewImportReferenceAfterCompaction", "RejectPreparedNewImportReferenceAfterCompaction",
    "RejectNewWideImportReferenceAfterCompaction", "PreparedGraphWithoutModuleIndicesSurvivesCompaction",
    "ImportAppendPreservesExistingGraphIndices", "RepeatedCommitPreservesPersistedDebugLinesAndColumns",
    "RunStopsAfterTaskFailure", "RunStopsAfterCollectionFailure",
    "RejectGraphAfterImportIndexCompaction", "RejectGraphAfterExportIndexCompaction",
    "CompactionRemapsSendableExternalModuleVarIndex", "CompactionRemapsWideSendableExternalModuleVarIndex",
    "SameGraphCanCommitSuccessiveEdits", "ReusedGraphRejectsAnotherGraphCommit",
    "PreparedAfterPublicCommitSurvivesGraphDestruction", "DifferentFunctionsCanCommitRepeatedlyInParallel",
    "FailedTaskDoesNotCreateOutput", "FailedTaskPreservesExistingOutput",
    "DynamicWriteWaitsForPublicMetadataRead", "DynamicCloseWaitsForPublicMetadataRead",
    "DynamicExtensionInspectConversionAndPropertyConcurrentRead",
    "DynamicReadUpgradeReturnsWrongState",
    "MetadataInspectFailsClosedWhenOwnerFileCannotBeResolved",
    "DynamicMetadataInspectFailsClosedWhenModuleFileIsMissing",
    "DynamicExternalModuleTargetChangesSerializeWithCoreReads",
    "DynamicRejectsStalePreparedFunction", "DynamicMetadataChangeRejectsPreparedFunction",
    "DynamicRejectsStaleGraphForSameFunction", "DynamicMetadataChangeRejectsStaleGraph",
    "DynamicLazyMetadataCacheConcurrentRead", "DynamicModifyValueAndLiteralCachesConcurrentCreate",
    "DynamicWriteFileRejectedWhileGraphAlive", "DynamicWriteFileRejectedWhilePreparedFunctionAlive",
    "DynamicCloseFileRejectedWhileGraphAlive", "DynamicCloseFileRejectedWhilePreparedFunctionAlive",
    "StaticInspectDoesNotUseDynamicFileLock", "StaticWriteDoesNotUseDynamicFileLock",
    "StaticSetGraphDoesNotUseDynamicFileLock", "StaticDestroyGraphDoesNotUseDynamicFileLock",
    "StaticCloseDoesNotUseDynamicLifecycleState",
}
NORMAL_SUITES = {
    "LibAbcKitGraphIsaIntegrationTest",
    "LibAbcKitStringStaticTest", "LibAbcKitStringDynamicTest", "LibAbcKitArrayStaticTest",
    "LibAbcKitArkTSInspectApiOpcodesStaticTest", "AbckitScenarioCTestClean", "AbckitScenarioCppTestClean",
    "LibAbcKitJSModifyApiModulesTest", "LibAbcKitModifyApiAnnotationsTests",
    "LibAbcKitArkTSModifyApiAnnotationsTest",
    "LibAbcKitDynamicCatchPhiTest", "LibAbcKitStaticCatchPhiTest",
    "LibAbcKitNullptrTestsInspectApiImpl0", "LibAbcKitNullptrTestsInspectApiImpl1",
    "LibAbcKitNullptrTestsArktsInspectApiImpl0", "LibAbcKitNullptrTestsJsInspectApiImpl0",
    "LibAbcKitNullptrTestsApiImpl0", "LibAbcKitNullptrTestsModifyApiImpl0",
    "LibAbcKitWrongCtxTestsModifyApiImpl0", "LibAbcKitWrongCtxTestsArktsModifyApiImpl0",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--tsan", action="store_true")
    parser.add_argument("--filter", help="Override the matrix for a focused diagnostic run")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    destination = args.evidence_dir.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    repo = Path(__file__).resolve().parents[2]
    binary = build / "tests/unittest/arkcompiler/runtime_core/libabckit/abckit_gtests"
    library = build / "arkcompiler/runtime_core/libabckit.so"
    build_args = (build / "args.gn").read_text()
    instrumentation = False
    if args.tsan:
        if not re.search(r"^\s*is_tsan\s*=\s*true\s*(?:#.*)?$", build_args, re.MULTILINE):
            parser.error("--tsan requires an is_tsan = true build")
        symbols = subprocess.check_output(["/usr/bin/nm", "-D", str(library)], text=True)
        binary_symbols = subprocess.check_output(["/usr/bin/nm", str(binary)], text=True)
        instrumentation = "__tsan_read" in symbols and "__tsan_init" in binary_symbols
        if not instrumentation:
            parser.error("TSAN instrumentation/runtime symbols are missing from library or executable")
    xml = destination / "results.xml"
    log = destination / "run.log"
    if xml.exists() or log.exists():
        parser.error("Use a new evidence directory; existing results must not be overwritten")
    command = (["/usr/bin/setarch", "x86_64", "-R"] if args.tsan else []) + [
        str(binary), "--gtest_filter=" + (args.filter or (TSAN if args.tsan else NORMAL)),
        "--gtest_output=xml:" + str(xml),
    ]
    env = os.environ.copy()
    for variable in ("GTEST_TOTAL_SHARDS", "GTEST_SHARD_INDEX", "GTEST_SHARD_STATUS_FILE",
                     "GTEST_REPEAT", "GTEST_ALSO_RUN_DISABLED_TESTS"):
        env.pop(variable, None)
    env["LD_LIBRARY_PATH"] = ":".join(str(build / part) for part in (
        "arkcompiler/runtime_core", "thirdparty/icu", "arkcompiler/ets_runtime", "thirdparty/zlib",
        "thirdparty/libuv", "arkui/napi", "thirdparty/bounds_checking_function",
    ))
    env.pop("ABCKIT_SCENARIO_SKIP_RUNTIME", None)
    if args.tsan:
        env["TSAN_OPTIONS"] = "halt_on_error=1:exitcode=66"
        env["ABCKIT_SCENARIO_SKIP_RUNTIME"] = "1"
    linked = subprocess.check_output(["/usr/bin/ldd", str(binary)], env=env, text=True)
    linked_paths = {Path(match) for match in re.findall(r"(?:=>\s+|^\s*)(/\S+)\s+\(", linked, re.MULTILINE)}
    provenance = {
        "command": command, "build_dir": str(build), "tsan": args.tsan,
        "matrix": "focused" if args.filter else ("tsan" if args.tsan else "normal"),
        "tsan_instrumentation_verified": instrumentation,
        "vm_execution": not args.tsan,
        "head": subprocess.check_output(["/usr/bin/git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
        "tracked_diff": subprocess.check_output(["/usr/bin/git", "diff", "HEAD"], cwd=repo, text=True),
        "binary_sha256": digest(binary), "library_sha256": digest(library),
        "linked_libraries": linked,
        "linked_libraries_sha256": {str(path): digest(path) for path in sorted(linked_paths)},
        "runtime_tools_sha256": {str(path): digest(path) for path in (
            build / "arkcompiler/ets_runtime/ark_js_vm", build / "arkcompiler/runtime_core/ark",
            build / "arkcompiler/ets_frontend/es2abc") if path.is_file()},
        "args_gn": build_args,
        "runner_sha256": digest(Path(__file__).resolve()),
        "source_sha256": {str(path.relative_to(repo)): digest(path)
                          for directory in ("src", "include", "tests/scenarios/thread_safety", "tests/ut/thread_safety")
                          for path in (repo / "libabckit" / directory).rglob("*")
                          if path.is_file() and path.suffix in (".cpp", ".h", ".ts", ".js")},
    }
    (destination / "provenance.json").write_text(json.dumps(provenance, indent=2))
    with log.open("w") as stream:
        try:
            result = subprocess.run(command, env=env, cwd=build.parents[1], stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
            code = result.returncode
        except subprocess.TimeoutExpired:
            code = 124
    report = {"matrix": provenance["matrix"], "exit_code": code, "tsan_reports": len(re.findall(
        r"(?:WARNING|ERROR|FATAL): ThreadSanitizer", log.read_text(errors="replace"))),
              "tests": 0, "executed": 0, "passed": 0, "disabled": 0,
              "failures": 0, "errors": 0, "rounds": {}, "scenarios": {},
              "required_core": {}, "completed_suites": [], "incomplete_tests": []}
    if xml.exists():
        root = ET.parse(xml).getroot()
        for key in ("tests", "failures", "errors", "disabled"):
            report[key] = int(root.get(key, "0"))
        for suite in root.findall("testsuite"):
            for test in suite.findall("testcase"):
                name = test.get("name")
                if test.get("status") == "run":
                    report["executed"] += 1
                completed = (test.get("status") == "run" and test.get("result") == "completed"
                             and test.find("failure") is None and test.find("skipped") is None)
                if completed:
                    report["passed"] += 1
                    if suite.get("name") not in report["completed_suites"]:
                        report["completed_suites"].append(suite.get("name"))
                elif not name.startswith("DISABLED_") and not suite.get("name", "").startswith("DISABLED_"):
                    report["incomplete_tests"].append(suite.get("name", "") + "." + name)
                if name in SCENARIOS:
                    report["scenarios"][name] = completed
                if name in REQUIRED_CORE:
                    report["required_core"][name] = completed
                rounds = {prop.get("name"): hashlib.sha256(prop.get("value", "").encode()).hexdigest()
                          for prop in test.findall("properties/property") if prop.get("name", "").startswith("round_")}
                if rounds:
                    report["rounds"][name] = rounds
    valid = code == 0 and report["passed"] > 0 and report["failures"] == 0 and report["errors"] == 0
    valid = valid and report["tsan_reports"] == 0 and not report["incomplete_tests"]
    if not args.filter:
        valid = valid and set(report["required_core"]) == REQUIRED_CORE and all(report["required_core"].values())
        if not args.tsan:
            valid = valid and NORMAL_SUITES <= set(report["completed_suites"])
        valid = valid and set(report["scenarios"]) == SCENARIOS and all(report["scenarios"].values())
        valid = valid and set(report["rounds"]) == ROUND_CASES
        valid = valid and all(set(rounds) == {"round_" + str(index) for index in range(10)}
                              and len(set(rounds.values())) == 1
                              for rounds in report["rounds"].values())
    report["valid"] = valid
    (destination / "summary.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return 0 if valid else 1


if __name__ == "__main__":
    sys.exit(main())
