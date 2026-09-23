# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0


class Scope:
    def __enter__(self):
        print("enter")
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        print("exit", exc_type.__name__ if exc_type else None)
        return False


class Holder:
    def __init__(self):
        self.scope = Scope()

    def run(self):
        with self.scope:
            try:
                raise ValueError("boom")
            finally:
                del self


try:
    Holder().run()
except ValueError as exc:
    print(type(exc).__name__, str(exc))
