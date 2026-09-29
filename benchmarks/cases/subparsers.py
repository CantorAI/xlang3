"""The pyperformance argparse benchmark for a parser with 1,000 options."""
import argparse


def generate_arguments(count):
    arguments = ["input.txt", "output.txt"]
    for index in range(count):
        arguments.extend((f"--option{index}", f"value{index}"))
    return arguments


def main():
    # Keep the official bm_argparse/subparsers shape: parser construction and
    # parsing 500 and 1,000 option/value pairs are all part of each iteration.
    parser = argparse.ArgumentParser()
    parser.add_argument("input_file", type=str, help="The input file")
    parser.add_argument("output_file", type=str, help="The output file")
    for index in range(1000):
        parser.add_argument(
            f"--option{index}", type=str, help=f"Optional argument {index}"
        )
    parser.parse_args(generate_arguments(500))
    parser.parse_args(generate_arguments(1000))


main()
