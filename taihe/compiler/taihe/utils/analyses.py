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

"""This module provides a framework for managing and caching instances of analysis objects.

The framework ensures that analyses are uniquely identified by their type and arguments,
avoiding redundant computation or memory usage.
"""

from abc import ABC, abstractmethod
from collections.abc import Hashable
from dataclasses import dataclass
from typing import Generic, NoReturn, TypeVar

P = TypeVar("P", bound=Hashable)
A = TypeVar("A", bound="AbstractAnalysis")


@dataclass(frozen=True)
class CacheKey:
    """Represents a unique key for identifying cached analysis instances."""

    analysis_type: type["AbstractAnalysis"]
    arg: Hashable


class AbstractAnalysis(ABC, Generic[P]):
    """Abstract Base class for all analyses.

    Enforcing the use of hashable argument for unique identification and caching.
    """

    def __new__(cls, *args, **kwargs) -> NoReturn:
        """Avoid accidentally instantiating without using the `get` method."""
        raise TypeError(
            f"Cannot instantiate {cls.__name__}. Use `{cls.__name__}.get` instead."
        )

    @abstractmethod
    def __init__(self, am: "AnalysisManager", arg: P) -> None:
        """Initialize analysis with hashable argument."""

    @classmethod
    def get(cls: type[A], am: "AnalysisManager", arg: P) -> A:
        """Get or create a cached analysis instance."""
        return am.get_or_create(cls, arg)


class AnalysisManager:
    """Manages caching and retrieval of analysis instances."""

    def __init__(self) -> None:
        self._cache: dict[CacheKey, AbstractAnalysis] = {}

    def get_or_create(self, analysis_type: type[A], arg: Hashable) -> A:
        """Get existing analysis or create new one if not cached."""
        key = CacheKey(analysis_type, arg)

        if cached := self._cache.get(key):
            assert isinstance(cached, analysis_type)
            return cached

        new_instance = object.__new__(analysis_type)
        new_instance.__init__(self, arg)
        self._cache[key] = new_instance
        return new_instance

    def clear(self) -> None:
        """Clear the analysis cache."""
        self._cache.clear()
