"""Exercise the running XLang3 demo through concurrent real HTTP requests."""

import argparse
import json
import statistics
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.request import Request, urlopen


def request(url, method="GET", payload=None):
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    headers = {} if body is None else {"Content-Type": "application/json"}
    started = time.monotonic()
    with urlopen(Request(url, data=body, headers=headers, method=method), timeout=10) as response:
        status = response.status
        content = response.read()
    elapsed = time.monotonic() - started
    return status, json.loads(content) if content else None, elapsed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:8765")
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--workers", type=int, default=16)
    parser.add_argument("--seconds", type=float, default=0)
    parser.add_argument("--implementation", default="xlang3")
    args = parser.parse_args()

    started = time.monotonic()
    latencies = []
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        if args.seconds:
            deadline = started + args.seconds

            def continuous_requests():
                samples = []
                while time.monotonic() < deadline:
                    samples.append(request(args.base + "/api/runtime"))
                return samples

            futures = [executor.submit(continuous_requests) for _ in range(args.workers)]
        else:
            futures = [executor.submit(request, args.base + "/api/runtime") for _ in range(args.requests)]
        for future in as_completed(futures):
            samples = future.result() if args.seconds else [future.result()]
            for status, data, elapsed in samples:
                assert status == 200 and data["implementation"] == args.implementation, (status, data)
                latencies.append(elapsed)
    duration = time.monotonic() - started
    latencies.sort()
    p95 = latencies[min(len(latencies) - 1, int(len(latencies) * 0.95))]
    print(json.dumps({
        "requests": len(latencies),
        "workers": args.workers,
        "errors": 0,
        "seconds": round(duration, 3),
        "requests_per_second": round(len(latencies) / duration, 1),
        "median_ms": round(statistics.median(latencies) * 1000, 1),
        "p95_ms": round(p95 * 1000, 1),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
