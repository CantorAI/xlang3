/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#pragma once

#include "test_harness.h"
#include "serialize/block_stream.h"
#include "serialize/ipc_value_marshal.h"
#include "xlang3/sequence.h"

namespace xlang3::test {

inline void check_strided_memoryview_ipc(CaseResult& result) {
  for (int64_t step : {int64_t{2}, int64_t{-2}}) {
    std::string error;
    auto parent = Value::memoryview(Value::bytearray("abcdef"), 0, 6, false);
    Value selected;
    expect_true(result, sequence_get_item(parent,
        Value::slice(Value::none(), Value::none(), Value::int64(step)), selected, error),
        "strided IPC source construction failed");
    if (selected.tag != ValueTag::Object) continue;
    // Only the derived export remains. The wire must contain logical bytes,
    // including reverse order, without depending on the parent view lifetime.
    value_set_none(parent);
    serialize::BlockStream stream;
    expect_true(result, stream.MarshalToBytes(selected, {}, error),
        "strided IPC memoryview marshal failed: " + error);
    stream.ResetPos();
    serialize::IpcWireValue wire;
    expect_true(result, stream.MarshalFromBytes(wire, error) &&
        wire.kind == serialize::IpcWireValueKind::Bytes &&
        wire.bytes == (step > 0 ? "ace" : "fdb"),
        "strided IPC marshal lost logical order: " + error);
  }
}

} // namespace xlang3::test
