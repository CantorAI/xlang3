def add3(a, b, c):
    return a + b + c

def main():
    total = 0.0
    i = 0
    while i < 3:
        total = total + add3(i, 2.5, 3.5)
        i = i + 1
    print(total)

main()
