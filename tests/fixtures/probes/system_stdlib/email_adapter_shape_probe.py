# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import email.message


class Wrapped(email.message.Message):
    def __new__(cls, orig):
        res = super().__new__(cls)
        vars(res).update(vars(orig))
        print("after-new", type(res), vars(res))
        return res

    def __init__(self, orig):
        print("before-read", type(self), vars(self))
        print("getattr-payload", getattr(self, "_payload"))
        print("payload", self._payload)


orig = email.message.Message()
orig._payload = "copied"
Wrapped(orig)
