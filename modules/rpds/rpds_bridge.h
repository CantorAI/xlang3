/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
struct X3RpdsMap;
struct X3RpdsMapSnapshot;
struct X3RpdsSet;
struct X3RpdsSetSnapshot;
struct X3RpdsList;
struct X3RpdsListSnapshot;
using X3RpdsRelease = void (*)(void*);
using X3RpdsEqual = int32_t (*)(void*, void*);

X3RpdsMap* x3_rpds_map_new();
X3RpdsMap* x3_rpds_map_clone(const X3RpdsMap*);
void x3_rpds_map_free(X3RpdsMap*);
size_t x3_rpds_map_len(const X3RpdsMap*);
X3RpdsMap* x3_rpds_map_insert(const X3RpdsMap*, void*, int64_t, void*,
                             X3RpdsRelease, X3RpdsEqual);
void* x3_rpds_map_get(const X3RpdsMap*, void*, int64_t, X3RpdsRelease,
                      X3RpdsEqual);
X3RpdsMap* x3_rpds_map_remove(const X3RpdsMap*, void*, int64_t,
                             X3RpdsRelease, X3RpdsEqual);
X3RpdsMapSnapshot* x3_rpds_map_snapshot(const X3RpdsMap*);
size_t x3_rpds_map_snapshot_len(const X3RpdsMapSnapshot*);
int32_t x3_rpds_map_snapshot_entry(const X3RpdsMapSnapshot*, size_t, void**,
                                  void**);
void x3_rpds_map_snapshot_free(X3RpdsMapSnapshot*);

X3RpdsSet* x3_rpds_set_new();
X3RpdsSet* x3_rpds_set_clone(const X3RpdsSet*);
void x3_rpds_set_free(X3RpdsSet*);
size_t x3_rpds_set_len(const X3RpdsSet*);
X3RpdsSet* x3_rpds_set_insert(const X3RpdsSet*, void*, int64_t,
                             X3RpdsRelease, X3RpdsEqual);
int32_t x3_rpds_set_contains(const X3RpdsSet*, void*, int64_t,
                             X3RpdsRelease, X3RpdsEqual);
X3RpdsSet* x3_rpds_set_remove(const X3RpdsSet*, void*, int64_t,
                             X3RpdsRelease, X3RpdsEqual);
X3RpdsSetSnapshot* x3_rpds_set_snapshot(const X3RpdsSet*);
size_t x3_rpds_set_snapshot_len(const X3RpdsSetSnapshot*);
void* x3_rpds_set_snapshot_item(const X3RpdsSetSnapshot*, size_t);
void x3_rpds_set_snapshot_free(X3RpdsSetSnapshot*);

X3RpdsList* x3_rpds_list_new();
X3RpdsList* x3_rpds_list_clone(const X3RpdsList*);
void x3_rpds_list_free(X3RpdsList*);
size_t x3_rpds_list_len(const X3RpdsList*);
X3RpdsList* x3_rpds_list_push_front(const X3RpdsList*, void*, X3RpdsRelease);
X3RpdsList* x3_rpds_list_drop_first(const X3RpdsList*);
X3RpdsListSnapshot* x3_rpds_list_snapshot(const X3RpdsList*);
size_t x3_rpds_list_snapshot_len(const X3RpdsListSnapshot*);
void* x3_rpds_list_snapshot_item(const X3RpdsListSnapshot*, size_t);
void x3_rpds_list_snapshot_free(X3RpdsListSnapshot*);
}
