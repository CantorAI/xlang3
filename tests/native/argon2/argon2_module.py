from _argon2_cffi_bindings import ffi, lib


password = b"correct horse battery staple"
salt = b"0123456789abcdef"
encoded_size = lib.argon2_encodedlen(2, 1024, 1, len(salt), 32, lib.Argon2_id) + 1
encoded = ffi.new("char[]", encoded_size)
status = lib.argon2_hash(
    2,
    1024,
    1,
    ffi.new("uint8_t[]", password),
    len(password),
    ffi.new("uint8_t[]", salt),
    len(salt),
    ffi.NULL,
    32,
    encoded,
    encoded_size,
    lib.Argon2_id,
    lib.ARGON2_VERSION_NUMBER,
)
encoded_hash = ffi.string(encoded)
print(status)
print(encoded_hash.decode("ascii"))
print(
    lib.argon2_verify(
        ffi.new("char[]", encoded_hash),
        ffi.new("uint8_t[]", password),
        len(password),
        lib.Argon2_id,
    )
)
print(
    lib.argon2_verify(
        ffi.new("char[]", encoded_hash),
        ffi.new("uint8_t[]", b"wrong"),
        5,
        lib.Argon2_id,
    )
)
print(ffi.string(lib.argon2_error_message(lib.ARGON2_VERIFY_MISMATCH)).decode("ascii"))
