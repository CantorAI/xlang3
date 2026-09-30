// Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
// Licensed under the Apache License, Version 2.0.

use regex::Regex;
use std::collections::{HashMap, VecDeque};
use std::ffi::{c_char, CString};
use std::slice;
use std::str;
use std::sync::{Arc, Mutex, OnceLock};

const CACHE_CAPACITY: usize = 50;

struct RegexCache {
    entries: HashMap<String, Arc<Regex>>,
    order: VecDeque<String>,
}

impl RegexCache {
    fn new() -> Self {
        Self {
            entries: HashMap::new(),
            order: VecDeque::new(),
        }
    }

    fn get(&mut self, pattern: &str) -> Option<Arc<Regex>> {
        let regex = self.entries.get(pattern)?.clone();
        if let Some(index) = self.order.iter().position(|key| key == pattern) {
            self.order.remove(index);
        }
        self.order.push_back(pattern.to_owned());
        Some(regex)
    }

    fn insert(&mut self, pattern: String, regex: Arc<Regex>) -> Arc<Regex> {
        if let Some(existing) = self.get(&pattern) {
            return existing;
        }
        if self.entries.len() == CACHE_CAPACITY {
            if let Some(oldest) = self.order.pop_front() {
                self.entries.remove(&oldest);
            }
        }
        self.order.push_back(pattern.clone());
        self.entries.insert(pattern, regex.clone());
        regex
    }
}

static CACHE: OnceLock<Mutex<RegexCache>> = OnceLock::new();

fn cached_regex(pattern: &str) -> Result<Arc<Regex>, String> {
    let cache = CACHE.get_or_init(|| Mutex::new(RegexCache::new()));
    if let Some(regex) = cache.lock().unwrap_or_else(|error| error.into_inner()).get(pattern) {
        return Ok(regex);
    }
    let regex = Arc::new(Regex::new(pattern).map_err(|error| error.to_string())?);
    Ok(cache
        .lock()
        .unwrap_or_else(|error| error.into_inner())
        .insert(pattern.to_owned(), regex))
}

unsafe fn input<'a>(data: *const u8, len: usize) -> Result<&'a str, String> {
    if data.is_null() && len != 0 {
        return Err("null regex input".to_owned());
    }
    let bytes = if len == 0 { &[] } else { slice::from_raw_parts(data, len) };
    str::from_utf8(bytes).map_err(|error| error.to_string())
}

unsafe fn set_error(output: *mut *mut c_char, message: String) {
    if !output.is_null() {
        let message = message.replace('\0', "\\0");
        *output = CString::new(message).unwrap().into_raw();
    }
}

#[no_mangle]
pub unsafe extern "C" fn x3_pydantic_regex_compile(
    pattern: *const u8,
    pattern_len: usize,
    error: *mut *mut c_char,
) -> i32 {
    if !error.is_null() {
        *error = std::ptr::null_mut();
    }
    match input(pattern, pattern_len).and_then(cached_regex) {
        Ok(_) => 1,
        Err(message) => {
            set_error(error, message);
            0
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn x3_pydantic_regex_is_match(
    pattern: *const u8,
    pattern_len: usize,
    target: *const u8,
    target_len: usize,
    error: *mut *mut c_char,
) -> i32 {
    if !error.is_null() {
        *error = std::ptr::null_mut();
    }
    let result = input(pattern, pattern_len)
        .and_then(cached_regex)
        .and_then(|regex| input(target, target_len).map(|target| regex.is_match(target)));
    match result {
        Ok(matched) => i32::from(matched),
        Err(message) => {
            set_error(error, message);
            -1
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn x3_pydantic_regex_error_free(error: *mut c_char) {
    if !error.is_null() {
        drop(CString::from_raw(error));
    }
}
