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

import argparse
import subprocess
import tomllib
from pathlib import Path

from win_no_popup import configure_no_popup_error_mode


ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "agent" / "config.toml"


def load_config() -> dict:
    return tomllib.loads(CONFIG_PATH.read_text(encoding="utf-8"))


def fixture_root(config: dict) -> Path:
    return ROOT / config.get("repo", {}).get("fixture_root", "tests/fixtures")


def default_xlang3(config: dict) -> Path:
    return ROOT / config.get("repo", {}).get("release_exe", "build/Release/xlang3.exe")


CORE_CASES = [
    "scalar_loop",
    "functions",
    "nested_function_no_closure",
    "if_else",
    "syntax_logical_lines",
    "syntax_simple_suites",
    "statement_syntax",
    "module_statement_partials",
    "structural_pattern_matching",
    "expression_operators",
    "chained_comparisons",
    "function_class_syntax",
    "future_annotations",
    "function_metadata",
    "object_type_model",
    "object_attribute_hooks",
    "descriptor_protocol",
    "member_descriptor_get",
    "abc_module_metadata",
    "sys_structseq_pickle",
    "code_traceback_model",
    "mro_model",
    "property_descriptor",
    "chained_object_methods",
    "builtin_alias",
    "builtin_function_batch",
    "dynamic_execution_builtins",
    "iterator_protocol",
    "tuples",
    "tuple_methods",
    "dict_views",
    "slices",
    "slots_model",
    "raw_strings",
    "string_compat",
    "binary_buffers",
    "starred_expressions",
    "dict_set_comprehensions",
    "nested_comprehensions",
    "generator_expressions",
    "walrus_operator",
    "unpacking",
    "annotated_assignment",
    "augmented_assignment",
    "lists_for",
    "sequences_index",
    "dict_set",
    "raw_blocks",
    "native_import",
    "json_module",
    "math_module",
    "time_module",
    "atexit_module",
    "io_os_modules",
    "io_module_streams",
    "imp_stat_modules",
    "collections_queue_modules",
    "collections_queue_facades",
    "types_module",
    "traceback_module",
    "linecache_module",
    "runpy_module",
    "importlib_module",
    "zlib_module",
    "zipfile_module",
    "weakref_module",
    "inspect_module",
    "inspect_currentframe",
    "debug_frame_metadata",
    "debug_breakpoint_step",
    "logging_pathlib_modules",
    "socket_select_modules",
    "file_import",
    "global_from_import",
    "package_import",
    "import_system_model",
    "vfs_file_io",
    "file_context_open",
    "file_io_compat",
    "exceptions",
    "runtime_error_exceptions",
    "exception_unwind_with",
    "typed_exceptions",
    "exception_chaining_sys",
    "finally_blocks",
    "classes",
    "class_dynamic_attrs",
    "context_managers",
    "builtin_methods",
    "threading_module",
    "trace_hooks",
    "trace_events",
    "trace_local_and_exception",
    "sys_coroutine_origin_metadata",
    "sys_cpu_count_config",
    "sys_command_path",
    "sys_module_doc_metadata",
    "sys_stdio_text_streams",
    "sys_path_importer_cache",
    "sys_dump_tracelets",
    "sys_monitoring_all_events",
    "task_async",
    "asyncio_module",
    "async_syntax",
    "async_protocols",
    "closures",
    "nonlocal_counter",
]


SECTION_CASES = [
    "module_and_statement_syntax",
    "function_and_class_syntax",
    "expression_syntax",
    "core_value_and_object_model",
    "functions_and_calls",
    "exceptions",
    "containers",
    "strings_and_unicode",
    "imports_and_modules",
    "builtins",
    "standard_modules",
]


NEGATIVE_CASES = [
    ("uncaught_exception", 1, ["runtime: uncaught exception", "top"]),
    ("uncaught_runtime_error", 1, ["runtime: uncaught exception", "division by zero"]),
    ("unset_instance_attr", 1, ["runtime: uncaught exception", "object has no attribute"]),
]


def normalize(text: str) -> str:
    return text.replace("\r\n", "\n").rstrip()


def read_expected(path: Path) -> str:
    return normalize(path.read_text(encoding="utf-8"))


def run_xlang3(xlang3: Path, source: Path, timeout: float) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            [str(xlang3), str(source)],
            cwd=ROOT,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        output = normalize((exc.stdout or "") + (exc.stderr or ""))
        detail = f"\nPartial output:\n{output}" if output else ""
        raise SystemExit(
            f"{source} timed out after {timeout:g} seconds.{detail}"
        ) from exc


def check_case(xlang3: Path, source: Path, expected_path: Path, label: str, timeout: float) -> None:
    result = run_xlang3(xlang3, source, timeout)
    if result.returncode != 0:
        raise SystemExit(f"{label} failed with exit code {result.returncode}")

    actual = normalize(result.stdout)
    expected = read_expected(expected_path)
    if actual != expected:
        raise SystemExit(
            f"{label} output mismatch.\nExpected:\n{expected}\nActual:\n{actual}"
        )

    print(f"{label} ok")


def check_negative(
    fixtures: Path,
    xlang3: Path,
    case: str,
    expected_code: int,
    required_fragments: list[str],
    timeout: float,
) -> None:
    source = fixtures / "core" / f"{case}.py"
    result = run_xlang3(xlang3, source, timeout)
    if result.returncode != expected_code:
        raise SystemExit(f"{case} expected exit code {expected_code}, got {result.returncode}")

    output = normalize(result.stdout + result.stderr)
    for fragment in required_fragments:
        if fragment not in output:
            raise SystemExit(f"{case} output missing {fragment!r}. Got:\n{output}")

    print(f"fixture {case} ok")


def main() -> int:
    configure_no_popup_error_mode()

    parser = argparse.ArgumentParser(description="Run XLang3 compatibility fixtures.")
    parser.add_argument("--xlang3", default="", help="Path to xlang3 executable.")
    parser.add_argument(
        "--case-timeout",
        type=float,
        default=60.0,
        help="Maximum seconds one fixture may run before it is treated as failed.",
    )
    args = parser.parse_args()

    config = load_config()
    fixtures = fixture_root(config)
    xlang3 = Path(args.xlang3) if args.xlang3 else default_xlang3(config)
    if not xlang3.is_absolute():
        xlang3 = ROOT / xlang3
    if not xlang3.exists():
        raise SystemExit(f"xlang3 executable not found: {xlang3}")

    for case in CORE_CASES:
        check_case(
            xlang3,
            fixtures / "core" / f"{case}.py",
            fixtures / "expected" / f"{case}.out",
            f"fixture {case}",
            args.case_timeout,
        )

    for case in SECTION_CASES:
        check_case(
            xlang3,
            fixtures / "compat_sections" / f"{case}.py",
            fixtures / "expected" / "compat_sections" / f"{case}.out",
            f"compat section {case}",
            args.case_timeout,
        )

    for case, code, fragments in NEGATIVE_CASES:
        check_negative(fixtures, xlang3, case, code, fragments, args.case_timeout)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
