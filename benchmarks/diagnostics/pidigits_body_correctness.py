"""Validate and optionally sample the unchanged official pidigits body.

Repeated body executions are for native IP sampling, not timing scores.
Imports and the first result are outside the optional fresh start marker.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('benchmark_script', type=Path)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--start-marker', type=Path)
    args = parser.parse_args()
    assert args.repeat > 0
    namespace = runpy.run_path(str(args.benchmark_script), run_name='pidigits_correctness_target')
    calculate = namespace['calc_ndigits']
    digits = calculate(2000)
    expected = hashlib.sha256(bytes(digits)).hexdigest()
    assert len(digits) == 2000 and digits[:10] == [3, 1, 4, 1, 5, 9, 2, 6, 5, 3]
    if args.start_marker is not None:
        assert not args.start_marker.exists()
        args.start_marker.write_text('body ready\n', encoding='utf-8')
    for _ in range(args.repeat):
        result = calculate(2000)
        assert len(result) == 2000
        assert hashlib.sha256(bytes(result)).hexdigest() == expected
    print(json.dumps({'digits': len(digits), 'sha256_digit_bytes': expected,
                      'first_32': digits[:32], 'last_32': digits[-32:],
                      'body_repetitions': args.repeat,
                      'benchmark_source_sha256': hashlib.sha256(args.benchmark_script.read_bytes()).hexdigest(),
                      'purpose': 'correctness and optional IP sampling only; not a timing result'}))


if __name__ == '__main__':
    main()
