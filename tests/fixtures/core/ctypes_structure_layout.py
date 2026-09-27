from ctypes import Structure, Union, alignment, c_char, c_int, c_short, sizeof


class ListFields(Structure):
    _fields_ = [('tag', c_char), ('count', c_int)]


class TupleFields(Structure):
    _fields_ = (('tag', c_char), ('count', c_int))


class Nested(Structure):
    _fields_ = (('small', c_short), ('record', TupleFields))


class Packed(Structure):
    _pack_ = 1
    _fields_ = (('tag', c_char), ('count', c_int))


class Choice(Union):
    _fields_ = (('tag', c_char), ('count', c_int))


for kind in (ListFields, TupleFields, Nested, Packed, Choice):
    print(kind.__name__, sizeof(kind), alignment(kind))
