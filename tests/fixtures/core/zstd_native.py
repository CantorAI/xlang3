from compression import zstd
import hashlib


payload = b"fastapi-zstd" * 1000
compressed = zstd.compress(payload)
print(zstd.zstd_version, len(payload), len(compressed) < len(payload))
print(zstd.decompress(compressed) == payload)

compressor = zstd.ZstdCompressor()
parts = [compressor.compress(payload[:5000]), compressor.compress(payload[5000:])]
parts.append(compressor.flush())
combined = b"".join(parts)
decoder = zstd.ZstdDecompressor()
decoded = decoder.decompress(combined[:10]) + decoder.decompress(combined[10:])
print(decoded == payload, decoder.eof, decoder.needs_input, decoder.unused_data)
print(zstd.get_frame_size(compressed) == len(compressed))

large = b"".join(hashlib.sha256(i.to_bytes(4, "little")).digest() for i in range(6000))
large_frame = zstd.compress(large)
large_decoder = zstd.ZstdDecompressor()
print(large_decoder.decompress(large_frame) == large, large_decoder.eof)
stream_decoder = zstd.ZstdDecompressor()
chunks = [large_frame[i:i + 1000] for i in range(0, len(large_frame), 1000)]
print(b"".join(stream_decoder.decompress(chunk) for chunk in chunks) == large,
      stream_decoder.eof)
