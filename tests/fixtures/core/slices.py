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

items = [0, 1, 2, 3, 4, 5]
print(items[:])
print(items[1:4])
print(items[:3])
print(items[3:])
print(items[-4:-1])
print(items[::2])
print(items[5:1:-2])
print(items[::-1])

text = "abcdef"
print(text[1:5])
print(text[::2])
print(text[::-1])

t = (10, 20, 30, 40, 50)
print(t[1:4])
print(t[::-2])

data = b"abcdef"
print(data[1:5])
print(data[::-2])

buffer = bytearray(b"abcdef")
print(buffer[1:5])
buffer[1:3] = b"XY"
print(buffer)
del buffer[2:4]
print(buffer)

items[1:4] = [9, 8]
print(items)
del items[::2]
print(items)

same_size = [0, 1, 2, 3, 4]
same_size[1:4] = [9, 8, 7]
print(same_size)
direct_source = [6, 5]
same_size[1:3] = direct_source
print(same_size)
same_size[1:3] = (4, 3)
print(same_size)
same_size[1:3] = same_size
print(same_size)
backward = [0, 1, 2, 3, 4]
backward[4:2] = [8, 9]
print(backward)

buffer = bytearray(b"abcdef")
buffer[1:5:2] = b"XY"
print(buffer)
del buffer[::-2]
print(buffer)

indexed = [10, 20, 30]
indexed[1] = 21
indexed[-1] = 31
print(indexed)
try:
    indexed[3] = 40
except Exception as error:
    print(type(error).__name__)

def reverse_prefix(values, index):
    values[:index + 1] = values[index::-1]
    return values

print(reverse_prefix([0, 1, 2, 3, 4], 3))
print(reverse_prefix([0, 1, 2, 3, 4], -2))
print(reverse_prefix([0, 1, 2, 3, 4], 100))
print(reverse_prefix([0, 1, 2, 3, 4], True))
print(reverse_prefix([0, 1, 2, 3, 4], -1))

def local_subscript(values, index):
    return values[index]

def local_first(values):
    return values[0]

def local_augment(values, index):
    values[index] -= 1
    return values

print(local_subscript([10, 20, 30], 1))
print(local_subscript([10, 20, 30], -1))
print(local_subscript([10, 20, 30], True))
print(local_first([10, 20, 30]))
print(local_augment([10, 20, 30], 1))
print(local_augment([10, 20, 30], -1))
print(local_augment([10, 20, 30], True))
print(local_augment([10.0, 20.0], 0))
print(local_augment([-9223372036854775807 - 1], 0))

def reverse_prefix_count(values, index):
    flips = 0
    while index:
        values[:index + 1] = values[index::-1]
        flips += 1
        index = values[0]
    return values, flips

print(reverse_prefix_count([2, 0, 1], 2))
print(reverse_prefix_count([1, 0, 2], True))
