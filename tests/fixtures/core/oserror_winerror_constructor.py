for code in (2, 6, 87, 258, 10060):
    error = OSError(0, "native failure", None, code, None)
    print(type(error).__name__, error.args[0], error.errno,
          error.winerror, error.strerror, error.filename, error.filename2)
