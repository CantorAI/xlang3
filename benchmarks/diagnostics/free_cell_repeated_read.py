"""Exercise one warmed free-variable load site with a stable closure cell."""


def make_reader():
    value = 42

    def read():
        return value

    return read


reader = make_reader()


def main():
    total = 0
    for _ in range(200000):
        total += reader()
    print(total)


main()
