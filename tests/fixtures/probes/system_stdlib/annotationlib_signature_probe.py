# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import annotationlib
import inspect


class AnnotatedClass:
    pass


AnnotatedClass.__annotations__ = {"x": "int"}


def annotated_function(x: "int") -> "str":
    return str(x)


print("class-ann", annotationlib.get_annotations(AnnotatedClass, format=annotationlib.Format.FORWARDREF))
print("func-ann", annotationlib.get_annotations(annotated_function, format=annotationlib.Format.FORWARDREF))
sig = inspect.signature(annotated_function, annotation_format=annotationlib.Format.FORWARDREF)
params = list(sig.parameters.values())
print("func-param-type", type(params[0]))
print("func-param-dict", getattr(type(params[0]), "__dict__", {}))
print("func-param-format", params[0]._format())
print("func-sig", sig)
