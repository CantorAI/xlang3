# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import annotationlib
import inspect


class Sample:
    def __init__(self, value: "int"):
        self.value = value


sig = inspect.signature(Sample, annotation_format=annotationlib.Format.FORWARDREF)
params = list(sig.parameters.values())
print("inspect-annotation-format-param", type(params[0]), hasattr(params[0], "_format"), params[0]._format())
print("inspect-annotation-format", str(sig))
