# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

print("prefix-start", flush=True)
import sys
print("prefix-sys", flush=True)
import _contextvars
print("prefix-contextvars", flush=True)
import _thread
print("prefix-thread", flush=True)

__all__ = ["warn", "warn_explicit", "showwarning"]
print("prefix-all", len(__all__), flush=True)

_wm = None
filters = []
defaultaction = "default"
onceregistry = {}
print("prefix-globals", flush=True)

_lock = _thread.RLock()
print("prefix-lock", _lock, flush=True)

_filters_version = 1
_use_context = sys.flags.context_aware_warnings
print("prefix-use-context", _use_context, flush=True)


class _Context:
    def __init__(self, filters):
        self._filters = filters
        self.log = None

    def copy(self):
        context = _Context(self._filters[:])
        if self.log is not None:
            context.log = self.log
        return context


print("prefix-context-class", flush=True)


class _GlobalContext(_Context):
    def __init__(self):
        self.log = None

    @property
    def _filters(self):
        try:
            return _wm.filters
        except AttributeError:
            return []


print("prefix-global-class", flush=True)
_global_context = _GlobalContext()
print("prefix-global-context", flush=True)
_warnings_context = _contextvars.ContextVar("warnings_context")
print("prefix-done", _warnings_context, flush=True)
