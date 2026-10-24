# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");

import sys


class MatrixLike:
    def __getitem__(self, index):
        if isinstance(index, tuple):
            return self.data[index[1]][index[0]]
        else:
            return self.data[index]


matrix = MatrixLike()
matrix.data = [[10, 11], [20, 21]]
print("integer", matrix[1])
object_rows = [matrix[index] for index in range(2)]
print("repeated-object-results", object_rows[0] is matrix.data[0], object_rows[1] is matrix.data[1])
print("tuple", matrix[1, 0])
print("negative", matrix[-1])
try:
    matrix[5]
except IndexError:
    print("out-of-range", "IndexError")


saved_isinstance = isinstance
isinstance_calls = []


def custom_isinstance(value, cls):
    isinstance_calls.append(cls)
    return False


isinstance = custom_isinstance
print("shadowed-builtin", matrix[0], bool(isinstance_calls))
isinstance = saved_isinstance


descriptor_reads = []


class DataDescriptor:
    def __get__(self, instance, owner):
        if instance is None:
            return self
        descriptor_reads.append("get")
        return instance.backing


class DescriptorMatrix(MatrixLike):
    data = DataDescriptor()


descriptor_matrix = DescriptorMatrix()
descriptor_matrix.backing = [[30, 31]]
print("descriptor", descriptor_matrix[0], descriptor_reads)


hook_reads = []


class HookMatrix(MatrixLike):
    def __getattribute__(self, name):
        if name == "data":
            hook_reads.append(name)
        return object.__getattribute__(self, name)


hook_matrix = HookMatrix()
hook_matrix.data = [[40, 41]]
print("custom-getattribute", hook_matrix[0], hook_reads)


def replacement_getitem(self, index):
    return self.data[index + 1]


MatrixLike.__getitem__.__code__ = replacement_getitem.__code__
print("code-replacement", matrix[0])

trace_calls = []


def trace_getitem(frame, event, arg):
    if frame.f_code.co_name in {"__getitem__", "replacement_getitem"} and event == "call":
        trace_calls.append("call")
    return trace_getitem


sys.settrace(trace_getitem)
try:
    traced_value = matrix[0]
finally:
    sys.settrace(None)
print("trace-frame", traced_value, bool(trace_calls))
