def main():
    limit = 9223372036854775807
    i = limit
    total = 1
    while i == limit:
        total = total + i * 3 - 7
        i = i + 1
    print(total)

main()
