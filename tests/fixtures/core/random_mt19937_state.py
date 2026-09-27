import _random
import random


for seed in (0, 90314, -90314, (1 << 130) + 90314, "abc", b"abc"):
    source = random.Random(seed)
    print(seed, [source.getrandbits(bits) for bits in (1, 7, 31, 32, 33, 64, 100)])
    print([source.random() for _ in range(3)])
    state = source.getstate()
    restored = random.Random()
    restored.setstate(state)
    print(len(state[1]), source.getrandbits(80) == restored.getrandbits(80))

native = _random.Random(90314)
state = native.getstate()
copy = _random.Random()
copy.setstate(state)
print(len(state), state[-1], native.random() == copy.random())
