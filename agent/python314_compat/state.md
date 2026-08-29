# State

Current phase:

```text
Python 3.14 runtime compatibility.
```

Current doctrine:

```text
Make the runtime compatible enough to run CPython standard-library .py files.
Do not grow native pure-Python stdlib clones as the main strategy.
```

Last stable compatibility checkpoint:

```text
os.stat_result tuple-subclass surface for os.stat and DirEntry.stat
```

Current next loop:

```text
Read agent/python314_compat/tasks/index.md.
Pick the next unfinished item from the selected compact task file.
Implement the runtime behavior.
Add fixture coverage under tests/fixtures.
Build with Visual Studio CMake.
Run agent/scripts/run_fixtures.py.
Commit only after tests pass.
If stopped or killed, rerun the same command and resume from loop_state.json.
```
