"""The pyperformance deepcopy_memo graph and repeated-copy loop."""
import copy


A = [1] * 100
data = {"a": (A, A, A), "b": [A] * 100}


def main():
    # Repeated aliases exercise deepcopy's integer-id memo lookups while
    # keeping the graph and operation identical to pyperformance 1.14.
    for _ in range(25):
        copy.deepcopy(data)


main()
