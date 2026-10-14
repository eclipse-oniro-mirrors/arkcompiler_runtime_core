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
import pathlib
import re

import pydantic

from src.path import normalize
from src.pydantic_helpers import PydanticModel
from src.schema import Label

RelativeParts = list[str]


class AppMetadata(PydanticModel):

    pid: pydantic.StrictInt
    label: Label
    bundle: str
    ability: str


class AppMetadataFile(PydanticModel):
    apps: list[AppMetadata]

    @pydantic.model_validator(mode="after")
    def validate_unique_labels(self) -> "AppMetadataFile":
        labels = [app.label for app in self.apps]
        if len(labels) != len(set(labels)):
            raise ValueError("duplicate app label")
        return self


class ArtifactMetadata(PydanticModel):
    label: Label
    timestamp: pydantic.StrictStr
    files: list[pydantic.StrictStr] = pydantic.Field(min_length=1)

    @pydantic.field_validator("timestamp")
    @classmethod
    def validate_timestamp(cls, value: str) -> str:
        if not value.isdecimal():
            raise ValueError("timestamp must be a nonnegative decimal integer")
        return value

    @pydantic.field_validator("files")
    @classmethod
    def validate_files(cls, files: list[str]) -> list[str]:
        for file in files:
            path = pathlib.PurePosixPath(file)
            if ("\\" in file or path.is_absolute() or str(path) != file
                    or file == "." or any(part == ".." for part in path.parts)):
                raise ValueError("file must be a portable relative path")
        if len(files) != len(set(files)):
            raise ValueError("duplicate artifact file")
        return files


class ArtifactMetadataFile(PydanticModel):
    artifacts: list[ArtifactMetadata]

    @pydantic.model_validator(mode="after")
    def validate_unique_identities(self) -> "ArtifactMetadataFile":
        labels = [artifact.label for artifact in self.artifacts]
        files = [file for artifact in self.artifacts for file in artifact.files]
        if len(labels) != len(set(labels)):
            raise ValueError("duplicate artifact label")
        if len(files) != len(set(files)):
            raise ValueError("duplicate artifact file")
        return self


@dataclasses.dataclass(frozen=True)
class AppSnapshotLabelPair:
    app_label: str
    snapshot_label: str


class ViewMetadata(PydanticModel):

    dirs: list[pathlib.Path] = pydantic.Field(min_length=1)
    smaps_filter: pydantic.StrictStr
    smaps_filter_flags: pydantic.StrictInt

    @pydantic.model_validator(mode="after")
    def validate_view_parameters(self) -> "ViewMetadata":
        dirs = [normalize(path) for path in self.dirs]
        if any(not path.is_absolute() for path in dirs):
            raise ValueError("view source directories must be absolute")
        if len(dirs) != len(set(dirs)):
            raise ValueError("duplicate view source directory")
        try:
            pattern = re.compile(self.smaps_filter, self.smaps_filter_flags)
        except (TypeError, ValueError, re.error) as error:
            raise ValueError("invalid smaps filter configuration") from error
        self.dirs = dirs
        self.smaps_filter_flags = int(pattern.flags)
        return self
