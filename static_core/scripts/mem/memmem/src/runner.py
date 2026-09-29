#!/usr/bin/env python3
# -- coding: utf-8 --
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

import dataclasses
import os
import pathlib
import subprocess
import time
import typing
from datetime import datetime

import pydantic

from src.commands import ExecutionContext, PendingArtifact, execute_command
from src.device import Device, DeviceHealth, ScreenBounds
from src.errors import CombinedError
from src.log import get_logger
from src.metadata import AppMetadataFile, AppMetadata, ArtifactMetadata, ArtifactMetadataFile
from src.result import ResultStore
from src.schema import AppFlow, Flow


@dataclasses.dataclass(frozen=True)
class BenchmarkOptions:
    out_dir: pathlib.Path
    reboot: bool = False
    device_log: bool = True


_HDC_WAIT_TIMEOUT_SECONDS = 120
_BOOT_COMPLETED_TIMEOUT_SECONDS = 180
_POST_REBOOT_DELAY_SECONDS = 5
_MIN_BATTERY_CAPACITY_PCT = 30
_MAX_THERMAL_TEMP_MILLIS_C = 90000


def create_result_store(options: BenchmarkOptions) -> ResultStore:
    remote_root = pathlib.PurePosixPath("/data/local/tmp")
    remote_name = f"memmem-out-{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}"
    return ResultStore(
        local_out_dir=options.out_dir,
        remote_out_dir=remote_root.joinpath(remote_name),
    )


def run_benchmark(
    flow: Flow,
    options: BenchmarkOptions,
    store: ResultStore,
    device: Device,
) -> None:
    context = _create_execution_context(flow, store, device)
    oldpwd = pathlib.Path.cwd()
    errors = CombinedError()
    get_logger().info("start benchmark pre flow")
    try:
        _pre_flow(flow, options, store, device, context)
    except Exception as error:
        errors.add(error, None)
    else:
        try:
            get_logger().info("start benchmark flow")
            os.chdir(options.out_dir)
            _flow(context)
        except Exception as error:
            errors.add(error, None)
        finally:
            try:
                _post_flow(store, context)
            except Exception as error:
                errors.add(error, "failed _post_flow step")
            try:
                _post_run_cleanup(store, context)
            except Exception as error:
                errors.add(error, "failed _post_run_cleanup step")
    finally:
        os.chdir(oldpwd)
    errors.raise_if_any()


def _pre_flow(
    flow: Flow,
    options: BenchmarkOptions,
    store: ResultStore,
    device: Device,
    context: ExecutionContext,
) -> None:
    logger = get_logger()
    logger.info(f"initializing local output: {store.local_out_dir()}")
    _initialize_local_output(flow, store, options)
    try:
        logger.info("preparing device")
        _prepare_device(device, options)
        logger.info("start device verification")
        _verify_device_ok(device.device_health())
        logger.info(f"initializing remote output: {store.remote_out_dir()}")
        _initialize_remote_output(device, store, options)
    except Exception as error:
        errors = CombinedError()
        errors.add(error, None)
        try:
            _post_flow(store, context)
        except Exception as post_flow_error:
            errors.add(post_flow_error, "failed post_flow step")
        errors.raise_if_any()

    try:
        context.screen_bounds = device.screen_bounds()
        if options.device_log:
            logger.info("starting device log capture")
            context.child_processes.append(
                device.start_log_capture(store.remote_hilog_path()))
            context.pending_artifacts.append(PendingArtifact(
                remote_base=store.remote_hilog_dir(),
                local_base=store.local_hilog_dir(),
                artifact=store.hilog_relative_parts(),
            ))
    except Exception as error:
        errors = CombinedError()
        errors.add(error, None)
        try:
            _post_flow(store, context)
        except Exception as post_flow_error:
            errors.add(post_flow_error, "failed post_flow step")
        try:
            _post_run_cleanup(store, context)
        except Exception as cleanup_error:
            errors.add(cleanup_error, "failed post_flow step")
        errors.raise_if_any()


def _create_execution_context(
    flow: Flow,
    store: ResultStore,
    device: Device,
) -> ExecutionContext:
    return ExecutionContext(
        device=device,
        store=store,
        flow=flow,
        launched_processes=[],
        active_processes=[],
        pending_artifacts=[],
        artifact_metadata={},
        child_processes=[],
        screen_bounds=ScreenBounds(0, 0, 0, 0),
    )


def _prepare_device(device: Device, options: BenchmarkOptions) -> None:
    if options.reboot:
        get_logger().info("rebooting device")
        device.reboot()
        get_logger().info("waiting for reboot to begin")
        time.sleep(_POST_REBOOT_DELAY_SECONDS)
        get_logger().info("waiting for device")
        device.wait_available(_HDC_WAIT_TIMEOUT_SECONDS)
        device.wait_boot_completed(_BOOT_COMPLETED_TIMEOUT_SECONDS)
        get_logger().info("waking device")
        device.wakeup()
        device.directional_fling("up", 20000, 20)
        device.send_key("Back")
    device.disable_screen_timeout()
    if options.device_log:
        device.configure_log_capture()


def _verify_device_ok(health: DeviceHealth) -> None:
    if health.battery_capacity_pct < _MIN_BATTERY_CAPACITY_PCT:
        raise RuntimeError(
            "device battery too low: "
            f"{health.battery_capacity_pct}% < {_MIN_BATTERY_CAPACITY_PCT}%"
        )
    for zone in health.thermal_zones:
        if zone.temp_millis_celsius == 0:
            continue
        if zone.temp_millis_celsius > _MAX_THERMAL_TEMP_MILLIS_C:
            raise RuntimeError(
                "device too hot: "
                f"{zone.name}={zone.temp_millis_celsius}mC > "
                f"{_MAX_THERMAL_TEMP_MILLIS_C}mC"
            )


def _flow(context: ExecutionContext) -> None:
    for app_flow in context.flow.flow:
        errors = CombinedError()
        try:
            get_logger().info("start application pre flow")
            _pre_app_flow(context, app_flow)
        except Exception as error:
            errors.add(error, None)
        else:
            try:
                get_logger().info("start application flow")
                _app_flow(context, app_flow)
            except Exception as error:
                errors.add(error, None)
        finally:
            try:
                get_logger().info("start application post flow")
                _post_app_flow(context, app_flow)
            except Exception as error:
                errors.add(error, "failed post_app_flow step")

        errors.raise_if_any()


def _pre_app_flow(
    context: ExecutionContext,
    app_flow: AppFlow,
) -> None:
    get_logger().info(
        f"launch app: label={app_flow.label} bundle={app_flow.bundle} ability={app_flow.ability}")
    context.device.launch_app(app_flow.bundle, app_flow.ability)
    pid = context.device.resolve_pid(app_flow.bundle)
    process = AppMetadata(
        pid=pid,
        label=app_flow.label,
        bundle=app_flow.bundle,
        ability=app_flow.ability,
    )
    context.launched_processes.append(process)
    context.active_processes.append(process)


def _app_flow(
    context: ExecutionContext,
    app_flow: AppFlow,
) -> None:
    for command in app_flow.commands:
        get_logger().info(
            f"executing command: app={app_flow.label} "
            f"action={typing.cast(str, getattr(command, 'action'))} "
            f"payload={command.payload.model_dump_json() if isinstance(command.payload, pydantic.BaseModel) else command.payload}")
        execute_command(command, context)


def _post_app_flow(
    context: ExecutionContext,
    app_flow: AppFlow,
) -> None:
    if app_flow.terminate:
        get_logger().info(
            f"terminating app: label={app_flow.label} bundle={app_flow.bundle}")
        context.active_processes = [
            process for process in context.active_processes
            if process.label != app_flow.label
        ]
        context.device.terminate_app(app_flow.bundle)


def _initialize_local_output(
    flow: Flow,
    store: ResultStore,
    options: BenchmarkOptions,
) -> None:
    store.local_out_dir().mkdir(parents=True, exist_ok=False)
    store.local_snapshots_dir().mkdir()
    store.local_screenshots_dir().mkdir()
    store.local_breakdowns_dir().mkdir()
    if options.device_log:
        store.local_hilog_dir().mkdir()
    _dump_model_to_json(flow, store.local_flow_path())


def _initialize_remote_output(
    device: Device,
    store: ResultStore,
    options: BenchmarkOptions,
) -> None:
    if not device.make_dir(store.remote_out_dir()):
        raise RuntimeError(
            f"failed to create remote run directory: {store.remote_out_dir()}")
    errors = CombinedError()
    try:
        if not device.make_dir(store.remote_snapshots_dir()):
            raise RuntimeError("failed to create remote snapshots directory")
        if not device.make_dir(store.remote_screenshots_dir()):
            raise RuntimeError("failed to create remote screenshots directory")
        if options.device_log and not device.make_dir(store.remote_hilog_dir()):
            raise RuntimeError("failed to create remote hilog directory")
    except Exception as error:
        errors.add(error, None)
        try:
            _cleanup_remote(device, store)
        except Exception as cleanup_error:
            errors.add(cleanup_error, "failed to clean up remote run directory")
        errors.raise_if_any()


def _dump_model_to_json(model: pydantic.BaseModel, path: pathlib.Path) -> None:
    path.write_text(model.model_dump_json(indent=2) + "\n", encoding="utf-8")


def _write_metadata(store: ResultStore, context: ExecutionContext) -> None:
    _dump_model_to_json(
        AppMetadataFile(apps=context.launched_processes),
        store.local_app_metadata_path(),
    )
    for local_base, metadata in context.artifact_metadata.items():
        _dump_model_to_json(
            ArtifactMetadataFile(artifacts=metadata),
            store.local_artifact_metadata_path(local_base),
        )


def _stop_process(
    process: subprocess.Popen[typing.Any],
    timeout_seconds: float = 5,
) -> None:
    returncode = process.poll()
    if returncode is not None:
        raise RuntimeError(
            "hilog stream exited before requested shutdown "
            f"with host code {returncode}; partial log will be preserved"
        )
    process.terminate()
    try:
        process.wait(timeout_seconds)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def _stop_child_processes(processes: list[subprocess.Popen[typing.Any]]) -> None:
    errors = CombinedError()
    for process in processes:
        try:
            _stop_process(process)
        except Exception as error:
            errors.add(error, "failed to stop child process")
    errors.raise_if_any()


def _post_flow(
    store: ResultStore,
    context: ExecutionContext,
) -> None:
    errors = CombinedError()
    get_logger().info("start benchmark post flow")
    try:
        get_logger().info("start child-process shutdown")
        _stop_child_processes(context.child_processes)
    except Exception as error:
        errors.add(error, "failed to stop child processes")

    try:
        get_logger().info("start metadata writing")
        _write_metadata(store, context)
    except Exception as error:
        errors.add(error, "failed to write metadata")

    try:
        get_logger().info(
            f"start artifact receiving: count={len(context.pending_artifacts)}")
        _receive_all_pending_artifacts(
            context.device, context.pending_artifacts)
    except Exception as error:
        errors.add(error, "failed to receive pending artifacts")
    errors.raise_if_any()


def _post_run_cleanup(
    store: ResultStore,
    context: ExecutionContext,
) -> None:
    errors = CombinedError()
    try:
        get_logger().info(f"start remote cleanup: {store.remote_out_dir()}")
        _cleanup_remote(context.device, store)
    except Exception as error:
        errors.add(error, "failed to clean up remote run directory")
    errors.raise_if_any()


def _receive_all_pending_artifacts(
    device: Device,
    artifacts: list[PendingArtifact],
) -> None:
    errors = CombinedError()
    for artifact in artifacts:
        try:
            _receive_pending_artifact(device, artifact)
        except Exception as error:
            errors.add(error, "failed to receive pending artifact")
    errors.raise_if_any()


def _receive_pending_artifact(
    device: Device,
    pending_artifact: PendingArtifact,
) -> None:
    remote_path = pending_artifact.remote_base.joinpath(
        *pending_artifact.artifact)
    local_path = pending_artifact.local_base.joinpath(
        *pending_artifact.artifact)
    # Hack for Hdc in wsl -- force relative pathing because hdc glues windows swd with linux abspath
    rel_local_path = pathlib.Path(os.path.relpath(local_path))
    result = device.recv_file(remote_path, rel_local_path)
    if result.is_error():
        raise RuntimeError(
            result.stderr.strip()
            or result.stdout.strip()
            or f"failed to receive {remote_path}"
        )


def _cleanup_remote(device: Device, store: ResultStore) -> None:
    if device.remove_dir(store.remote_out_dir()):
        return None
    raise RuntimeError(
        f"failed to clean up remote run directory: {store.remote_out_dir()}")


def _combine_errors(
    primary: Exception,
    secondary: Exception,
    secondary_context: str,
) -> RuntimeError:
    return RuntimeError(f"{primary}; additionally {secondary_context}: {secondary}")
