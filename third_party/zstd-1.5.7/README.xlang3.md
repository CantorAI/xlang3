Vendored Zstandard C library `lib/` from facebook/zstd tag `v1.5.7`,
commit `f8745da6ff1ad1e7bab384bd1f9d742439278e99`.

Source: https://github.com/facebook/zstd/tree/v1.5.7/lib
License: `LICENSE` (BSD option of the upstream dual license).

XLang3 builds this C library into its `_zstd` native package. It does not
load CPython's `_zstd.pyd` or depend on the CPython runtime.
