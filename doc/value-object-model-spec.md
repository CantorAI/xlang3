<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# Value And Object Model Spec

Status: draft 0

## Goals

1. Preserve XLang's universal value idea.
2. Store scalars directly in the value carrier.
3. Store complex values as objects.
4. Keep refcounted ownership.
5. Make hot scalar and object dispatch paths easy to specialize.

## X3Value

`X3Value` is the universal runtime value.

Required value kinds:

```text
Invalid
None
Bool
Int64
UInt64
Double
Object
SmallString optional later
```

Initial representation:

```c
typedef enum X3ValueTag {
    X3_TAG_INVALID = 0,
    X3_TAG_NONE,
    X3_TAG_BOOL,
    X3_TAG_INT64,
    X3_TAG_UINT64,
    X3_TAG_DOUBLE,
    X3_TAG_OBJECT
} X3ValueTag;

typedef struct X3Value {
    uint32_t tag;
    uint32_t flags;
    union {
        int64_t i64;
        uint64_t u64;
        double f64;
        X3Object* obj;
    } as;
} X3Value;
```

This may later be optimized to NaN boxing or tagged pointers, but only after correctness is stable.

## Scalar Fast Paths

Binary operations must check direct scalar cases before object dispatch:

```c
X3Status x3_binary_add(X3Runtime* rt, X3Value a, X3Value b, X3Value* out) {
    if (a.tag == X3_TAG_INT64 && b.tag == X3_TAG_INT64) {
        *out = x3_make_int(a.as.i64 + b.as.i64);
        return X3_OK;
    }
    if ((a.tag == X3_TAG_DOUBLE || a.tag == X3_TAG_INT64) &&
        (b.tag == X3_TAG_DOUBLE || b.tag == X3_TAG_INT64)) {
        *out = x3_make_double(x3_to_double(a) + x3_to_double(b));
        return X3_OK;
    }
    return x3_object_binary_add(rt, a, b, out);
}
```

Overflow behavior must be decided before implementation:

- Option A: wrap int64
- Option B: promote to big-int object
- Option C: raise overflow

Default for Python compatibility should be B eventually, but Phase 0 may use A with explicit TODO.

## Object Header

Every object starts with:

```c
typedef struct X3Object {
    X3Type* type;
    uint32_t refcnt;
    uint32_t flags;
} X3Object;
```

Concrete objects embed this as first field.

## Type

```c
typedef struct X3Type {
    const char* name;
    uint32_t flags;
    uint32_t version;
    X3TypeSlots slots;
} X3Type;
```

`version` changes when layout/method table invalidates inline caches.

## Shape

Objects with dynamic attributes should have a shape:

```c
typedef struct X3Shape {
    uint32_t version;
    uint32_t attr_count;
    /* name -> slot mapping */
} X3Shape;
```

Attribute inline caches should use:

```text
type pointer
shape/version
slot offset
```

## Builtin Object Types

Minimum core objects:

```text
str
list
dict
tuple
set
function
native_function
module
package
type
slice
range
error/exception
iterator
```

Optional/standard module objects:

```text
array
tensor
binary
table
remote object
deferred object
```

## Value Lifetime

`X3Value` copies are cheap. Copying an object value must retain reference when it is stored beyond the current borrowed use.

Frame slots own values.

Containers own values.

Temporary registers in interpreter frames own values unless using a borrow-optimized temporary policy. Phase 0 should use clear ownership first.

## Avoiding Virtual Dispatch

No runtime object behavior should require C++ virtual calls.

Use:

```text
X3Value -> X3Object* -> X3Type* -> X3TypeSlots
```

Executors may specialize:

```c
if (obj->type == &X3List_Type && index.tag == X3_TAG_INT64) {
    return x3_list_get_item_fast(...);
}
```

