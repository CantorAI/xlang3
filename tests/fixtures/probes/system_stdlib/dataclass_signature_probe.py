# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import annotationlib
import dataclasses
import inspect


@dataclasses.dataclass(frozen=True, kw_only=True)
class KwOnlyBox:
    name: str = "demo"
    color: str = "blue"


sig = inspect.signature(KwOnlyBox, annotation_format=annotationlib.Format.FORWARDREF)
params = list(sig.parameters.values())
print("dataclass-signature", type(params[0]), hasattr(params[0], "_format"), params[0]._format(), str(sig), KwOnlyBox().name)
