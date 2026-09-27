for number in (2, 3, 4, 11, 13, 17, 32, 10035, 10053, 10054, 10060, 10061):
    error = OSError(number, 'probe')
    print(number, type(error).__name__, error.errno, error.strerror)
