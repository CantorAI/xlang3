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
from __future__ import annotations

import os


def configure_no_popup_error_mode() -> None:
    if os.name != "nt":
        return

    try:
        import ctypes

        sem_failcriticalerrors = 0x0001
        sem_nogpfault_errorbox = 0x0002
        sem_noopenfile_errorbox = 0x8000
        mode = sem_failcriticalerrors | sem_nogpfault_errorbox | sem_noopenfile_errorbox
        ctypes.windll.kernel32.SetErrorMode(mode)
    except Exception:
        # This is a best-effort console hygiene setting. If Windows refuses it,
        # the runner should continue and report the real test failure.
        return
