# -*- coding: utf-8 -*-
#
# Copyright (c) 2024 Huawei Device Co., Ltd.
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
#

import dataclasses
from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import Any, AsyncIterator, Callable, Dict, List, Literal, Optional, Tuple, Type, TypeAlias

import trio
import trio_cdp
from cdp import debugger, runtime

from arkdb.compiler import StringCodeCompiler
from arkdb.debug_connection import ArkConnection, Proxy, ScriptsCache, SourcesCache, connect_cdp

T: TypeAlias = trio_cdp.T


@dataclass
class DebuggerConfig:
    pause_on_exceptions_mode: Literal["none", "caught", "uncaught", "all"] = "none"


class DebuggerClient:
    def __init__(
        self,
        connection: ArkConnection,
        config: DebuggerConfig,
        debugger_id: runtime.UniqueDebuggerId,
        scripts: ScriptsCache,
        sources: SourcesCache,
        context: runtime.ExecutionContextDescription,
        code_compiler: StringCodeCompiler,
    ) -> None:
        self.connection = connection
        self.config = config
        self.debugger_id = debugger_id
        self.scripts = scripts
        self.sources = sources
        self.context = context
        self.code_compiler = code_compiler

    async def configure(self, nursery: trio.Nursery):
        self._listen(nursery, self._on_script_parsed)
        await self.set_pause_on_exceptions()

    async def run_if_waiting_for_debugger(self) -> debugger.Paused:
        return await self.connection.send_and_wait_for(
            runtime.run_if_waiting_for_debugger(),
            debugger.Paused,
        )

    @asynccontextmanager
    async def wait_for(self, event_type: Type[T], buffer_size=1):
        proxy: Proxy[T]
        async with self.connection.wait_for(event_type=event_type, buffer_size=buffer_size) as proxy:
            yield proxy

    async def set_pause_on_exceptions(
        self,
        mode: Optional[Literal["none", "caught", "uncaught", "all"]] = None,
    ):
        await self.connection.send(
            debugger.set_pause_on_exceptions(
                mode if mode is not None else self.config.pause_on_exceptions_mode,
            ),
        )

    async def resume(self) -> debugger.Resumed:
        return await self.connection.send_and_wait_for(
            debugger.resume(),
            debugger.Resumed,
        )

    async def resume_and_wait_for_paused(self) -> debugger.Paused:
        async with self.wait_for(debugger.Paused) as proxy:
            await self.resume()
        await trio.lowlevel.checkpoint()
        return proxy.value

    async def continue_to_location(
        self,
        script_id: runtime.ScriptId,
        line_number: int,
    ) -> debugger.Paused:
        return await self.connection.send_and_wait_for(
            debugger.continue_to_location(
                location=debugger.Location(
                    script_id=script_id,
                    line_number=line_number,
                ),
            ),
            debugger.Paused,
        )

    async def get_script_source(
        self,
        script_id: runtime.ScriptId,
    ) -> str:
        return await self.connection.send(debugger.get_script_source(script_id))

    async def get_script_source_cached(
        self,
        script_id: runtime.ScriptId,
    ) -> str:
        return await self.sources.get(script_id, self.get_script_source)

    async def get_properties(
        self,
        object_id: runtime.RemoteObjectId,
        own_properties: Optional[bool] = None,
        accessor_properties_only: Optional[bool] = None,
        generate_preview: Optional[bool] = None,
    ) -> Tuple[
        List[runtime.PropertyDescriptor],
        Optional[List[runtime.InternalPropertyDescriptor]],
        Optional[List[runtime.PrivatePropertyDescriptor]],
        Optional[runtime.ExceptionDetails],
    ]:
        return await self.connection.send(
            runtime.get_properties(
                object_id=object_id,
                own_properties=own_properties,
                accessor_properties_only=accessor_properties_only,
                generate_preview=generate_preview,
            )
        )

    async def set_breakpoint(
        self,
        location: debugger.Location,
        condition: Optional[str] = None,
    ) -> Tuple[debugger.BreakpointId, debugger.Location]:
        return await self.connection.send(
            debugger.set_breakpoint(
                location=location,
                condition=condition,
            ),
        )

    async def set_breakpoint_by_url(
        self,
        line_number: int,
        url: Optional[str] = None,
        url_regex: Optional[str] = None,
        script_hash: Optional[str] = None,
        column_number: Optional[int] = None,
        condition: Optional[str] = None,
    ) -> Tuple[debugger.BreakpointId, List[debugger.Location]]:
        return await self.connection.send(
            debugger.set_breakpoint_by_url(
                line_number=line_number,
                url=url,
                url_regex=url_regex,
                script_hash=script_hash,
                column_number=column_number,
                condition=condition,
            ),
        )

    async def get_possible_breakpoints(
        self,
        start: debugger.Location,
        end: Optional[debugger.Location] = None,
        restrict_to_function: Optional[bool] = None,
    ) -> List[debugger.BreakLocation]:
        return await self.connection.send(
            debugger.get_possible_breakpoints(
                start=start,
                end=end,
                restrict_to_function=restrict_to_function,
            )
        )

    async def set_breakpoints_active(self, active: bool) -> None:
        await self.connection.send(debugger.set_breakpoints_active(active=active))

    async def evaluate(self, expression: str) -> tuple[runtime.RemoteObject, runtime.ExceptionDetails | None]:
        return await self.connection.send(runtime.evaluate(expression))

    def _on_script_parsed(self, event: debugger.ScriptParsed):
        pass

    def _listen(
        self,
        nursery: trio.Nursery,
        handler: Callable[[T], None],
    ):
        async def _t():
            async for event in self.connection.listen(T):
                handler(event)

        nursery.start_soon(_t)


@asynccontextmanager
async def create_debugger_client(
    connection: ArkConnection,
    scripts: ScriptsCache,
    sources: SourcesCache,
    code_compiler: StringCodeCompiler,
    debugger_config: DebuggerConfig = DebuggerConfig(),
) -> AsyncIterator[DebuggerClient]:
    context = await connection.send_and_wait_for(
        runtime.enable(),
        runtime.ExecutionContextCreated,
    )
    debugger_id = await connection.send(
        debugger.enable(),
    )
    yield DebuggerClient(
        connection=connection,
        config=debugger_config,
        debugger_id=debugger_id,
        scripts=scripts,
        sources=sources,
        context=context.context,
        code_compiler=code_compiler,
    )


class BreakpointManager:

    def __init__(self, client: DebuggerClient) -> None:
        self._lock = trio.Lock()
        self.client = client
        self._breaks: Dict[debugger.BreakpointId, List[debugger.Location]] = dict()

    async def set_by_url(self, line_number: int, url: Optional[str]) -> None:
        await self.client.set_breakpoints_active(True)
        br, locs = await self.client.set_breakpoint_by_url(line_number=line_number, url=url)
        async with self._lock:
            self._breaks[br] = locs

    async def get(self, bp_id: debugger.BreakpointId) -> Optional[List[debugger.Location]]:
        async with self._lock:
            return self._breaks.get(bp_id)

    @asynccontextmanager
    async def get_all(self):
        async with self._lock:
            breaks = self._breaks.copy()
        for br, locs in breaks:
            yield (br, locs)
            await trio.lowlevel.checkpoint()

    async def get_possible_breakpoints(self) -> Dict[debugger.BreakpointId, List[debugger.BreakLocation]]:
        # Сhrome does this after set_breakpoint_by_url
        async with self.get_all() as pair:
            return {
                br: br_locs
                async for br, locs in pair
                for br_locs in [
                    await self.client.get_possible_breakpoints(
                        start=dataclasses.replace(loc, column_number=0),
                        end=dataclasses.replace(loc, column_number=1),
                    )
                    for loc in locs
                ]
            }


class DebugLocator:
    scripts: ScriptsCache
    sources: SourcesCache

    def __init__(self, code_compiler: StringCodeCompiler, url: Any) -> None:
        self.code_compiler = code_compiler
        self.url = url
        self.scripts = ScriptsCache()
        self.sources = SourcesCache()

    @asynccontextmanager
    async def connect(self, nursery: trio.Nursery) -> AsyncIterator[DebuggerClient]:
        cdp = await connect_cdp(nursery, self.url, 10)
        async with cdp:
            connection = ArkConnection(cdp, nursery)
            async with create_debugger_client(
                connection,
                self.scripts,
                self.sources,
                self.code_compiler,
            ) as debugger_client:
                yield debugger_client