  expect_true(result, cache.index != UINT32_MAX &&
      xlang_vm_load_attr_cached(receiver, "x", cache, found, error) &&
      cache.kind == AttrSiteKind::InstanceSlot && found.as.i64 == 47,
      "unsafe old cache release must not poison the following safe canonical hit");

