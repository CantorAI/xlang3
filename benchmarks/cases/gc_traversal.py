"""Repeated collection over the pyperformance gc_traversal object graph."""
import gc


def create_recursive_containers(n_levels):
    current_list = []
    for n in range(n_levels):
        new_list = [None] * n
        for index in range(n):
            new_list[index] = current_list
        current_list = new_list
    return current_list


# Keep graph construction outside main(): the fixed-baseline gate times only
# collection, matching pyperformance's bench_time_func accounting.
all_levels = create_recursive_containers(1000)


def main():
    gc.collect()  # unmeasured warm-up, as in the official benchmark
    for _ in range(3):
        gc.collect()
    print(len(all_levels), len(all_levels[-1]))


main()
