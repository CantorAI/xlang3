import sys
import re


def check_text(characters):
    text = "".join(characters)
    assert len(text) == len(characters)
    check_search(characters)
    # List indexing/slicing is an independent oracle for codepoint boundaries.
    for repeat in range(12):
        for index in (0, 1, len(characters) // 2, len(characters) - 1, -1, -2):
            assert text[index] == characters[index]
            assert str.__getitem__(text, index) == characters[index]
        for start, stop, step in (
            (None, None, 1), (None, None, -1), (None, None, 3),
            (None, None, -7), (1, -1, 1), (-3, None, 1),
            (30, 33, 1), (1000, 1004, 1), (19, 3, 1),
            (None, None, 2**63 - 1), (None, None, -(2**63)),
        ):
            key = slice(start, stop, step)
            assert text[key] == "".join(characters[key])
            assert str.__getitem__(text, key) == "".join(characters[key])
    for index in (len(characters), -len(characters) - 1):
        try:
            text[index]
        except IndexError:
            pass
        else:
            raise AssertionError("out-of-range string index accepted")
    try:
        text[::0]
    except ValueError:
        pass
    else:
        raise AssertionError("zero slice step accepted")
    try:
        str.__getitem__(text, slice(None, None, 0))
    except ValueError:
        pass
    else:
        raise AssertionError("native zero slice step accepted")
    # New results and retained aliases must keep their own valid metadata.
    alias = text
    text = "prefix" + text + "\U0001f600"
    assert len(alias) == len(characters) and alias[-1] == characters[-1]
    assert text[6:-1] == alias and text[-1] == "\U0001f600"
    assert alias.strip() == alias and len(alias.strip()) == len(characters)
    interned = sys.intern(alias)
    assert interned == alias and len(interned) == len(characters)
    assert interned[-1] == characters[-1]
    return alias


def check_search(characters):
    text = "".join(characters)
    n = len(characters)
    # Compute match positions from character lists, independently of str search.
    for needle_chars in ([], characters[:2], characters[-2:], ["!", "?"]):
        needle = "".join(needle_chars)
        width = len(needle_chars)
        for start in (-n - 2, -1, 0, 1, 127, 128, n - 1, n, n + 1):
            for end in (-n - 2, -1, 0, 128, n, n + 1):
                lo = max(0, start + n) if start < 0 else start
                hi = min(n, max(0, end + n) if end < 0 else end)
                positions = [i for i in range(lo, hi - width + 1)
                             if i <= n and characters[i:i + width] == needle_chars]
                first = positions[0] if positions else -1
                last = positions[-1] if positions else -1
                assert text.find(needle, start, end) == first
                assert text.rfind(needle, start, end) == last
                assert text.startswith(needle, start, end) == (first == lo and first != -1)
                assert text.endswith(needle, start, end) == (last == hi - width and last != -1)
                count = 0
                next_position = lo
                for position in positions:
                    if position >= next_position:
                        count += 1
                        next_position = position + max(width, 1)
                assert text.count(needle, start, end) == count
                if first != -1:
                    assert text.index(needle, start, end) == first
                    assert text.rindex(needle, start, end) == last
                else:
                    for method in (text.index, text.rindex):
                        try:
                            method(needle, start, end)
                        except ValueError:
                            pass
                        else:
                            raise AssertionError("missing substring accepted")


for size in (7, 65, 129, 1025):
    check_text(["a"] * size)
    check_text(["a"] * (size - 2) + ["\u00e9", "\U0001f600"])
    check_text((["a", "\u00e9", "\U0001f600", "\u6f22", "\ud800", "\udfff", "z"] * size)[:size])


class Source(str):
    pass


source = Source("a" * 2048 + "\u00e9\U0001f600")
assert len(source) == 2050 and source[-2:] == "\u00e9\U0001f600"
assert str.__getitem__(source, -1) == "\U0001f600"


class Override(str):
    def __len__(self):
        return 7

    def __getitem__(self, key):
        return ("override", key)


source = Override("a" * 2048 + "\u00e9")
assert len(source) == 7 and source[3] == ("override", 3)
for size in (65, 129, 257):
    for prefix in ("a" * size, "a" * (size - 2) + "é😀", "é" * size):
        text = prefix + "é😀123456"
        n = len(prefix)
        pattern = re.compile(r"(é)(😀)(123)(456)()")
        match = pattern.match(text, n, n + 8)
        assert match.string is text and match.pos == n and match.endpos == n + 8
        assert match.span() == (n, n + 8)
        assert match.groups() == ("é", "😀", "123", "456", "")
        assert match.regs == ((n, n + 8), (n, n + 1), (n + 1, n + 2),
                              (n + 2, n + 5), (n + 5, n + 8), (n + 8, n + 8))
        assert match.expand(r"\1:\2:\3:\4") == "é:😀:123:456"
        assert pattern.match(text, n, n + 7) is None
        assert pattern.fullmatch(text, n).span() == (n, n + 8)
        del text
        assert match.group() == "é😀123456" and match.group(2) == "😀"
        # Repeated capture repair must also return character positions.
        repeated = re.compile(r"(é)+()").match(prefix + "éé", n)
        assert repeated.groups() == ("é", "")
        assert repeated.regs == ((n, n + 2), (n + 1, n + 2), (n + 2, n + 2))
for pattern, text in ((re.compile(b"a"), "é"), (re.compile("a"), b"a")):
    try:
        pattern.match(text)
    except TypeError:
        pass
    else:
        raise AssertionError("mixed regex operand types accepted")


print("unicode index cache semantics ok")
