"""Isolate CALL_FUNCTION_EX binding for one starred positional sequence."""


def collect(*items, label="default"):
    return items


def main():
    values = (1, 2, 3, 4, 5, 6)
    total = 0
    for _ in range(200000):
        total += collect(*values)[0]
    print(total)


main()
