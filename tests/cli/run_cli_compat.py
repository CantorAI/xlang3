# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import pathlib
import os
import subprocess
import sys


def assert_output(name, expected, command, cwd=None, env=None):
    result = subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True)
    actual = result.stdout.replace("\r\n", "\n").rstrip()
    if result.returncode or actual != expected:
        raise RuntimeError(
            f"{name} failed ({result.returncode})\nexpected={expected!r}"
            f"\nactual={actual!r}\nstderr={result.stderr}"
        )


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    work = pathlib.Path(sys.argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = "import sys\nprint(sys.argv)"
    assert_output("command argv", "['-c', 'alpha', 'beta']", [executable, "-c", source, "alpha", "beta"])

    script = work / "script_argv.py"
    script.write_text(source, encoding="utf-8")
    assert_output("script argv", repr([str(script), "one", "two"]), [executable, str(script), "one", "two"])

    module = work / "cli_module_probe.py"
    module.write_text(source, encoding="utf-8")
    assert_output("module argv", repr([str(module), "red", "blue"]), [executable, "-m", "cli_module_probe", "red", "blue"], work)
    assert_output("ignored -X", "['-c', 'gamma']", [executable, "-X", "frozen_modules=off", "-c", source, "gamma"])
    assert_output("ignored compact -X", "['-c', 'delta']", [executable, "-Xdev", "-c", source, "delta"])
    assert_output("version", "Python 3.14.7", [executable, "--version"])
    flags_source = (
        "import sys; print(sys.flags.dont_write_bytecode, sys.flags.ignore_environment, "
        "sys.flags.no_user_site, sys.flags.no_site, sys.flags.safe_path, "
        "sys.flags.optimize, sys.flags.bytes_warning, sys.flags.quiet, "
        "sys.flags.dev_mode, sys.flags.utf8_mode, sys.dont_write_bytecode, sys.warnoptions)"
    )
    assert_output(
        "startup flags",
        "1 1 1 1 True 2 2 1 True 1 True ['default', 'error', 'error::BytesWarning']",
        [executable, "-B", "-E", "-s", "-S", "-P", "-OO", "-bb", "-q", "-Werror", "-Xdev", "-Xutf8", "-c", flags_source],
    )

    env_module_dir = work / "environment_modules"
    env_module_dir.mkdir(exist_ok=True)
    (env_module_dir / "ide_environment_probe.py").write_text("VALUE = 17\n", encoding="utf-8")
    environment = dict(os.environ)
    environment["PYTHONPATH"] = str(env_module_dir)
    environment_source = "import ide_environment_probe; print(ide_environment_probe.VALUE)"
    assert_output("environment path", "17", [executable, "-S", "-c", environment_source], env=environment)
    ignored_environment_source = (
        "import importlib.util; print(importlib.util.find_spec('ide_environment_probe') is None)"
    )
    assert_output("ignored environment path", "True", [executable, "-E", "-S", "-c", ignored_environment_source], env=environment)

    package = work / "cli_package_dir"
    package.mkdir(exist_ok=True)
    (package / "__main__.py").write_text(source, encoding="utf-8")
    assert_output("directory argv", repr([str(package), "left", "right"]), [executable, str(package), "left", "right"])

    path_package = work / "cli_path_package_dir"
    path_package.mkdir(exist_ok=True)
    (path_package / "__main__.py").write_text(
        "import sys\nsys.path[0] = sys.path[0] + '/../'\n"
        "print(sys.path[0])\ndel sys.path[0]\nprint(len(sys.path) > 0)",
        encoding="utf-8",
    )
    assert_output("directory sys.path mutation", f"{path_package}/../\nTrue", [executable, str(path_package)])


if __name__ == "__main__":
    main()
