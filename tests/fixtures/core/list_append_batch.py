def main():
    items = []
    i = 0
    while i < 5:
        items.append(i)
        i = i + 1

    # The fusion must leave an overflowing increment to the generic bigint path.
    edge = []
    i = 9223372036854775806
    while i < 9223372036854775807:
        edge.append(i)
        i = i + 10

    print(items, edge, i)

main()
