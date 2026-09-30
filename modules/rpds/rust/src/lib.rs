// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// Licensed under the Apache License, Version 2.0.
//
// The C++ package owns XLang3 values. Rust owns opaque boxes through release;
// snapshots retain the persistent trie so borrowed boxes remain valid.

use rpds::{HashTrieMap, HashTrieSet, List};
use std::ffi::c_void;
use std::hash::{Hash, Hasher};
use std::rc::Rc;

pub type Release = unsafe extern "C" fn(*mut c_void);
pub type Equal = unsafe extern "C" fn(*mut c_void, *mut c_void) -> i32;

struct ForeignValue {
    pointer: *mut c_void,
    release: Release,
}

impl Drop for ForeignValue {
    fn drop(&mut self) {
        unsafe { (self.release)(self.pointer) }
    }
}

type Value = Rc<ForeignValue>;

#[derive(Clone)]
struct Key {
    value: Value,
    hash: i64,
    equal: Equal,
}

impl Hash for Key {
    fn hash<H: Hasher>(&self, state: &mut H) {
        state.write_i64(self.hash);
    }
}

impl PartialEq for Key {
    fn eq(&self, other: &Self) -> bool {
        unsafe { (self.equal)(self.value.pointer, other.value.pointer) != 0 }
    }
}

impl Eq for Key {}

fn key(pointer: *mut c_void, hash: i64, release: Release, equal: Equal) -> Key {
    Key {
        value: Rc::new(ForeignValue { pointer, release }),
        hash,
        equal,
    }
}

fn value(pointer: *mut c_void, release: Release) -> Value {
    Rc::new(ForeignValue { pointer, release })
}

pub struct Map(HashTrieMap<Key, Value>);
pub struct Set(HashTrieSet<Key>);
pub struct PersistentList(List<Value>);

pub struct MapSnapshot {
    // A clone is cheap and keeps all borrowed pointers alive.
    _owner: HashTrieMap<Key, Value>,
    entries: Vec<(Value, Value)>,
}

pub struct SetSnapshot {
    _owner: HashTrieSet<Key>,
    entries: Vec<Value>,
}

pub struct ListSnapshot {
    _owner: List<Value>,
    entries: Vec<Value>,
}

#[no_mangle]
pub extern "C" fn x3_rpds_map_new() -> *mut Map {
    Box::into_raw(Box::new(Map(HashTrieMap::new())))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_clone(map: *const Map) -> *mut Map {
    Box::into_raw(Box::new(Map((*map).0.clone())))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_free(map: *mut Map) {
    if !map.is_null() {
        drop(Box::from_raw(map));
    }
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_len(map: *const Map) -> usize {
    (*map).0.size()
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_insert(
    map: *const Map,
    key_pointer: *mut c_void,
    key_hash: i64,
    value_pointer: *mut c_void,
    release: Release,
    equal: Equal,
) -> *mut Map {
    let next = (*map).0.insert(
        key(key_pointer, key_hash, release, equal),
        value(value_pointer, release),
    );
    Box::into_raw(Box::new(Map(next)))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_get(
    map: *const Map,
    key_pointer: *mut c_void,
    key_hash: i64,
    release: Release,
    equal: Equal,
) -> *mut c_void {
    let query = key(key_pointer, key_hash, release, equal);
    (*map)
        .0
        .get(&query)
        .map_or(std::ptr::null_mut(), |item| item.pointer)
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_remove(
    map: *const Map,
    key_pointer: *mut c_void,
    key_hash: i64,
    release: Release,
    equal: Equal,
) -> *mut Map {
    let query = key(key_pointer, key_hash, release, equal);
    Box::into_raw(Box::new(Map((*map).0.remove(&query))))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_snapshot(map: *const Map) -> *mut MapSnapshot {
    let owner = (*map).0.clone();
    let entries = owner
        .iter()
        .map(|(key, value)| (key.value.clone(), value.clone()))
        .collect();
    Box::into_raw(Box::new(MapSnapshot {
        _owner: owner,
        entries,
    }))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_snapshot_len(snapshot: *const MapSnapshot) -> usize {
    (*snapshot).entries.len()
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_snapshot_entry(
    snapshot: *const MapSnapshot,
    index: usize,
    key_pointer: *mut *mut c_void,
    value_pointer: *mut *mut c_void,
) -> i32 {
    match (&(*snapshot).entries).get(index) {
        Some((key, value)) => {
            *key_pointer = key.pointer;
            *value_pointer = value.pointer;
            1
        }
        None => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_map_snapshot_free(snapshot: *mut MapSnapshot) {
    if !snapshot.is_null() {
        drop(Box::from_raw(snapshot));
    }
}

#[no_mangle]
pub extern "C" fn x3_rpds_set_new() -> *mut Set {
    Box::into_raw(Box::new(Set(HashTrieSet::new())))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_clone(set: *const Set) -> *mut Set {
    Box::into_raw(Box::new(Set((*set).0.clone())))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_free(set: *mut Set) {
    if !set.is_null() {
        drop(Box::from_raw(set));
    }
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_len(set: *const Set) -> usize {
    (*set).0.size()
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_insert(
    set: *const Set,
    key_pointer: *mut c_void,
    key_hash: i64,
    release: Release,
    equal: Equal,
) -> *mut Set {
    Box::into_raw(Box::new(Set((*set).0.insert(key(
        key_pointer,
        key_hash,
        release,
        equal,
    )))))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_contains(
    set: *const Set,
    key_pointer: *mut c_void,
    key_hash: i64,
    release: Release,
    equal: Equal,
) -> i32 {
    let query = key(key_pointer, key_hash, release, equal);
    i32::from((*set).0.contains(&query))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_remove(
    set: *const Set,
    key_pointer: *mut c_void,
    key_hash: i64,
    release: Release,
    equal: Equal,
) -> *mut Set {
    let query = key(key_pointer, key_hash, release, equal);
    Box::into_raw(Box::new(Set((*set).0.remove(&query))))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_snapshot(set: *const Set) -> *mut SetSnapshot {
    let owner = (*set).0.clone();
    let entries = owner.iter().map(|key| key.value.clone()).collect();
    Box::into_raw(Box::new(SetSnapshot {
        _owner: owner,
        entries,
    }))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_snapshot_len(snapshot: *const SetSnapshot) -> usize {
    (*snapshot).entries.len()
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_snapshot_item(
    snapshot: *const SetSnapshot,
    index: usize,
) -> *mut c_void {
    (&(*snapshot).entries)
        .get(index)
        .map_or(std::ptr::null_mut(), |value| value.pointer)
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_set_snapshot_free(snapshot: *mut SetSnapshot) {
    if !snapshot.is_null() {
        drop(Box::from_raw(snapshot));
    }
}

#[no_mangle]
pub extern "C" fn x3_rpds_list_new() -> *mut PersistentList {
    Box::into_raw(Box::new(PersistentList(List::new())))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_clone(list: *const PersistentList) -> *mut PersistentList {
    Box::into_raw(Box::new(PersistentList((*list).0.clone())))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_free(list: *mut PersistentList) {
    if !list.is_null() {
        drop(Box::from_raw(list));
    }
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_len(list: *const PersistentList) -> usize {
    (*list).0.len()
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_push_front(
    list: *const PersistentList,
    pointer: *mut c_void,
    release: Release,
) -> *mut PersistentList {
    Box::into_raw(Box::new(PersistentList(
        (*list).0.push_front(value(pointer, release)),
    )))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_drop_first(
    list: *const PersistentList,
) -> *mut PersistentList {
    match (*list).0.drop_first() {
        Some(next) => Box::into_raw(Box::new(PersistentList(next))),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_snapshot(list: *const PersistentList) -> *mut ListSnapshot {
    let owner = (*list).0.clone();
    let entries = owner.iter().cloned().collect();
    Box::into_raw(Box::new(ListSnapshot {
        _owner: owner,
        entries,
    }))
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_snapshot_len(snapshot: *const ListSnapshot) -> usize {
    (*snapshot).entries.len()
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_snapshot_item(
    snapshot: *const ListSnapshot,
    index: usize,
) -> *mut c_void {
    (&(*snapshot).entries)
        .get(index)
        .map_or(std::ptr::null_mut(), |value| value.pointer)
}

#[no_mangle]
pub unsafe extern "C" fn x3_rpds_list_snapshot_free(snapshot: *mut ListSnapshot) {
    if !snapshot.is_null() {
        drop(Box::from_raw(snapshot));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Mutex;

    static RELEASES: AtomicUsize = AtomicUsize::new(0);
    static TEST_LOCK: Mutex<()> = Mutex::new(());

    unsafe extern "C" fn release(pointer: *mut c_void) {
        RELEASES.fetch_add(1, Ordering::SeqCst);
        drop(Box::from_raw(pointer as *mut i64));
    }

    unsafe extern "C" fn equal(left: *mut c_void, right: *mut c_void) -> i32 {
        i32::from(*(left as *const i64) == *(right as *const i64))
    }

    fn boxed(number: i64) -> *mut c_void {
        Box::into_raw(Box::new(number)).cast()
    }

    #[test]
    fn map_snapshots_preserve_old_values_and_release_each_box_once() {
        let _guard = TEST_LOCK.lock().unwrap();
        let before = RELEASES.load(Ordering::SeqCst);
        unsafe {
            let empty = x3_rpds_map_new();
            let first = x3_rpds_map_insert(empty, boxed(1), 1, boxed(10), release, equal);
            let second = x3_rpds_map_insert(first, boxed(2), 2, boxed(20), release, equal);
            assert_eq!(x3_rpds_map_len(empty), 0);
            assert_eq!(x3_rpds_map_len(first), 1);
            assert_eq!(x3_rpds_map_len(second), 2);
            let old = x3_rpds_map_snapshot(first);
            x3_rpds_map_free(first);
            let mut key = std::ptr::null_mut();
            let mut value = std::ptr::null_mut();
            assert_eq!(x3_rpds_map_snapshot_entry(old, 0, &mut key, &mut value), 1);
            assert_eq!(*(key as *const i64), 1);
            assert_eq!(*(value as *const i64), 10);
            assert_eq!(
                *(x3_rpds_map_get(second, boxed(2), 2, release, equal) as *const i64),
                20
            );
            x3_rpds_map_snapshot_free(old);
            x3_rpds_map_free(second);
            x3_rpds_map_free(empty);
        }
        assert_eq!(RELEASES.load(Ordering::SeqCst) - before, 5);
    }

    #[test]
    fn set_and_list_snapshots_preserve_old_versions() {
        let _guard = TEST_LOCK.lock().unwrap();
        let before = RELEASES.load(Ordering::SeqCst);
        unsafe {
            let empty_set = x3_rpds_set_new();
            let first_set = x3_rpds_set_insert(empty_set, boxed(4), 4, release, equal);
            let second_set = x3_rpds_set_insert(first_set, boxed(5), 5, release, equal);
            let old_set = x3_rpds_set_snapshot(first_set);
            assert_eq!(x3_rpds_set_snapshot_len(old_set), 1);
            assert_eq!(*(x3_rpds_set_snapshot_item(old_set, 0) as *const i64), 4);
            assert_eq!(
                x3_rpds_set_contains(second_set, boxed(5), 5, release, equal),
                1
            );
            assert_eq!(
                x3_rpds_set_contains(first_set, boxed(5), 5, release, equal),
                0
            );
            x3_rpds_set_free(first_set);
            x3_rpds_set_free(second_set);
            x3_rpds_set_free(empty_set);
            x3_rpds_set_snapshot_free(old_set);

            let empty_list = x3_rpds_list_new();
            let first_list = x3_rpds_list_push_front(empty_list, boxed(10), release);
            let second_list = x3_rpds_list_push_front(first_list, boxed(20), release);
            let old_list = x3_rpds_list_snapshot(first_list);
            assert_eq!(x3_rpds_list_snapshot_len(old_list), 1);
            assert_eq!(*(x3_rpds_list_snapshot_item(old_list, 0) as *const i64), 10);
            let tail = x3_rpds_list_drop_first(second_list);
            assert_eq!(x3_rpds_list_len(tail), 1);
            assert_eq!(x3_rpds_list_drop_first(empty_list), std::ptr::null_mut());
            x3_rpds_list_free(tail);
            x3_rpds_list_free(second_list);
            x3_rpds_list_free(first_list);
            x3_rpds_list_free(empty_list);
            x3_rpds_list_snapshot_free(old_list);
        }
        // Two set values, two lookups, and two list values.
        assert_eq!(RELEASES.load(Ordering::SeqCst) - before, 6);
    }

    #[test]
    fn equal_hashes_are_disambiguated_by_foreign_equality() {
        let _guard = TEST_LOCK.lock().unwrap();
        let before = RELEASES.load(Ordering::SeqCst);
        unsafe {
            let empty = x3_rpds_map_new();
            let first = x3_rpds_map_insert(empty, boxed(1), 7, boxed(10), release, equal);
            let second = x3_rpds_map_insert(first, boxed(2), 7, boxed(20), release, equal);
            assert_eq!(x3_rpds_map_len(second), 2);
            assert_eq!(
                *(x3_rpds_map_get(second, boxed(1), 7, release, equal) as *const i64),
                10
            );
            assert_eq!(
                *(x3_rpds_map_get(second, boxed(2), 7, release, equal) as *const i64),
                20
            );
            x3_rpds_map_free(second);
            x3_rpds_map_free(first);
            x3_rpds_map_free(empty);
        }
        assert_eq!(RELEASES.load(Ordering::SeqCst) - before, 6);
    }
}
