# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


def first(record):
    return record[0]


def second(record):
    return record[1]


def controls_factory():
    getters = [first, second]
    return (lambda rec: (getters[0](rec), getters[1](rec)),
            lambda rec: tuple(getter(rec) for getter in getters))


ordinary, generator = controls_factory()
assert ordinary((3, 5)) == (3, 5)
assert generator((3, 5)) == (3, 5)
print("PASS ordinary_and_generator_controls")


def eager_factory():
    getters = [first, second]
    return (lambda rec: tuple([getter(rec) for getter in getters]),
            lambda rec: {getter(rec) for getter in getters},
            lambda rec: {index: getter(rec)
                         for index, getter in enumerate(getters)})


listed, grouped, mapped = eager_factory()
assert listed((3, 5)) == (3, 5)
assert grouped((3, 5)) == {3, 5}
assert mapped((3, 5)) == {0: 3, 1: 5}
print("PASS returned_lambda_all_eager_types")


def shadow_factory():
    values = (2, 4)
    return (lambda: [values for values in values],
            lambda: {values for values in values},
            lambda: {values: values * 2 for values in values})


listed, grouped, mapped = shadow_factory()
assert listed() == [2, 4]
assert grouped() == {2, 4}
assert mapped() == {2: 4, 4: 8}
print("PASS first_iterable_before_target_shadow")


def parameter_factory():
    values = "outer value must not replace a lambda parameter"
    return (lambda values: [values for values in values],
            lambda values: {values for values in values},
            lambda values: {values: values * 2 for values in values})


listed, grouped, mapped = parameter_factory()
assert listed((6, 8)) == [6, 8]
assert grouped((6, 8)) == {6, 8}
assert mapped((6, 8)) == {6: 12, 8: 16}
print("PASS lambda_parameter_scope")


def clauses_factory():
    values = (1, 2, 3)
    threshold = 2
    extras = (4, 5)
    ceiling = 5
    scale = 10
    return (lambda: [value * scale + extra
                     for value in values if value >= threshold
                     for extra in extras if extra < ceiling],
            lambda: {value * scale + extra
                     for value in values if value >= threshold
                     for extra in extras if extra < ceiling},
            lambda: {value: value * scale + extra
                     for value in values if value >= threshold
                     for extra in extras if extra < ceiling})


listed, grouped, mapped = clauses_factory()
assert listed() == [24, 34]
assert grouped() == {24, 34}
assert mapped() == {2: 24, 3: 34}
print("PASS filters_results_and_multiple_clauses")


def escaping_factory():
    sentinel = 99
    offset = 7
    return (lambda values: ([lambda: (item, offset) for item in values], sentinel),
            lambda values: ({lambda: (item, offset) for item in values}, sentinel),
            lambda values: ({item: lambda: (item, offset)
                             for item in values}, sentinel))


listed, grouped, mapped = escaping_factory()
list_old, outer = listed((1, 2))
assert outer == 99
list_new, outer = listed((3, 4))
assert outer == 99
assert [getter() for getter in list_old] == [(2, 7), (2, 7)]
assert [getter() for getter in list_new] == [(4, 7), (4, 7)]
set_old, outer = grouped((1, 2))
assert outer == 99
set_new, outer = grouped((3, 4))
assert outer == 99
assert {getter() for getter in set_old} == {(2, 7)}
assert {getter() for getter in set_new} == {(4, 7)}
dict_old, outer = mapped((1, 2))
assert outer == 99
dict_new, outer = mapped((3, 4))
assert outer == 99
assert [dict_old[index]() for index in (1, 2)] == [(2, 7), (2, 7)]
assert [dict_new[index]() for index in (3, 4)] == [(4, 7), (4, 7)]
print("PASS nested_lambda_late_binding_and_escaping_cells")


def iterable_lambda_factory():
    values = (2, 3)
    return ([value for value in (lambda: values)()],
            {value for value in (lambda: values)()},
            {value: value * 2 for value in (lambda: values)()})


assert iterable_lambda_factory() == ([2, 3], {2, 3}, {2: 4, 3: 6})
print("PASS first_iterable_creates_lambda")


def transitive_factory():
    rows = ((1, 2), (3,))
    offset = 5
    return lambda: lambda: (
        [[value + offset for value in row] for row in rows],
        {value + offset for row in rows for value in row},
        {value: value + offset for row in rows for value in row})


def rebound_factory():
    values = (1, 2)
    listed = lambda: [value + offset for value in values]
    grouped = lambda: {value + offset for value in values}
    mapped = lambda: {value: value + offset for value in values}
    offset = 10
    values = (3, 4)
    offset = 20
    return listed, grouped, mapped


assert transitive_factory()()() == ([[6, 7], [8]], {6, 7, 8}, {1: 6, 2: 7, 3: 8})
listed, grouped, mapped = rebound_factory()
assert listed() == [23, 24]
assert grouped() == {23, 24}
assert mapped() == {3: 23, 4: 24}
print("PASS transitive_nested_comprehensions_and_rebinding")
