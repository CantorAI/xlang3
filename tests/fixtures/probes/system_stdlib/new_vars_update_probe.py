# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

class Source:
    def __init__(self):
        self._payload = "copied"


class Wrapper(Source):
    def __new__(cls, orig):
        res = super().__new__(cls)
        vars(res).update(vars(orig))
        return res

    def __init__(self, orig):
        print("new-vars-update", self._payload, vars(self))


Wrapper(Source())
