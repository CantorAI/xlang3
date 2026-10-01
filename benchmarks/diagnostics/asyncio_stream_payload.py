"""Check asyncio stream payload integrity without reporting benchmark scores.

The write/drain/read shape matches pyperformance's asyncio_tcp workload.
Use a dynamic port and expose the received size to localize truncated data.
The official benchmark remains unchanged and is required for timing claims.
"""
import argparse
import asyncio
import collections


def trace_overlapped():
    import _overlapped
    constructor = _overlapped.Overlapped
    dequeue = _overlapped.GetQueuedCompletionStatus
    counts = collections.Counter()
    kinds = {}

    class TracedOverlapped:
        def __init__(self, event):
            self.operation = constructor(event)
            self.kind = "other"

        def __getattr__(self, name):
            return getattr(self.operation, name)

        def WSARecvInto(self, handle, buffer, flags):
            self.kind = "recv_into"
            kinds[self.operation.address] = self.kind
            return self.operation.WSARecvInto(handle, buffer, flags)

        def WSASend(self, handle, data, flags):
            self.kind = "send"
            kinds[self.operation.address] = self.kind
            counts["send_requested_bytes"] += len(data)
            return self.operation.WSASend(handle, data, flags)

        def getresult(self, *args):
            result = self.operation.getresult(*args)
            if self.kind in ("recv_into", "send"):
                counts[self.kind + "_results"] += 1
                counts[self.kind + "_bytes"] += result
                if result == 0:
                    counts[self.kind + "_zero_results"] += 1
            return result

    _overlapped.Overlapped = TracedOverlapped
    def traced_dequeue(port, timeout):
        packet = dequeue(port, timeout)
        if packet is not None:
            error, size, key, address = packet
            kind = kinds.get(address, "other")
            counts[kind + "_packet_bytes"] += size
            counts[kind + "_packets"] += 1
            if error:
                counts[kind + "_packet_errors"] += 1
        return packet
    _overlapped.GetQueuedCompletionStatus = traced_dequeue
    return counts


async def transfer(chunk_size, chunks, check_content=True):
    payload = b"x" * chunk_size
    sent = 0
    closed = asyncio.Event()

    async def send_payload(reader, writer):
        nonlocal sent
        try:
            for _ in range(chunks):
                writer.write(payload)
                await writer.drain()
                sent += len(payload)
            writer.close()
            await writer.wait_closed()
        finally:
            closed.set()

    server = await asyncio.start_server(send_payload, "127.0.0.1", 0)
    async with server:
        asyncio.create_task(server.start_serving())
        port = server.sockets[0].getsockname()[1]
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        received = 0
        reads = 0
        valid = True
        while True:
            data = await reader.read(chunk_size)
            if not data:
                break
            received += len(data)
            reads += 1
            if check_content:
                valid = valid and data == b"x" * len(data)
        writer.close()
        await writer.wait_closed()
        await closed.wait()
    expected = chunk_size * chunks
    print("payload", "expected", expected, "sent", sent, "received", received,
          "reads", reads, "valid", valid, flush=True)
    assert sent == received == expected and valid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chunk-size", type=int, default=10 * 1024 * 1024)
    parser.add_argument("--chunks", type=int, default=100)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--trace-overlapped", action="store_true",
                        help="Wrap native operations to count transfer sizes; changes timing")
    parser.add_argument("--skip-content-check", action="store_true",
                        help="Use only the official workload's length assertion to probe timing races")
    args = parser.parse_args()
    counts = trace_overlapped() if args.trace_overlapped else None
    try:
        async def repeat():
            for _ in range(args.repeat):
                await transfer(args.chunk_size, args.chunks, not args.skip_content_check)
        asyncio.run(repeat())
    finally:
        if counts is not None:
            for name, value in sorted(counts.items()):
                print("overlapped", name, value, flush=True)


if __name__ == "__main__":
    main()
