# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import codecs
import encodings.aliases
import unicodedata as u

points = [
    0x0000, 0x000A, 0x0020, 0x0041, 0x005F, 0x00B2, 0x00DF, 0x0130,
    0x01C5, 0x0301, 0x0345, 0x03A3, 0x03C2, 0x05D0, 0x0660, 0x1680,
    0x200B, 0x2028, 0x20AC, 0x212A, 0x2163, 0x24D0, 0x4E00, 0xAC00,
    0xFB03, 0x10400, 0x10428, 0x1D7D8, 0x1F600, 0x31350, 0x10FFFF,
]
print('unicode-version', u.unidata_version)
for cp in points:
    char = chr(cp)
    print(
        hex(cp), u.name(char, None), u.category(char), u.bidirectional(char),
        u.combining(char), u.east_asian_width(char), u.mirrored(char),
        u.decimal(char, None), u.digit(char, None), u.numeric(char, None),
        u.decomposition(char), char.islower(), char.isupper(), char.istitle(),
        char.isalpha(), char.isdigit(), char.isdecimal(), char.isnumeric(),
        char.isalnum(), char.isspace(), char.isprintable(), char.isidentifier(),
        ('A' + char).isidentifier(),
    )

for requested in (
    'latin small letter sharp s', 'LF', 'BYTE ORDER MARK',
    'HANGUL SYLLABLE GAG', 'CJK UNIFIED IDEOGRAPH-31350', 'KEYCAP NUMBER SIGN',
):
    print('lookup', requested, [hex(ord(char)) for char in u.lookup(requested)])

for text in ('Straße', 'İSTANBUL', 'ΟΣ', 'ΟΣΑ', 'AΣ', 'AΣ́', 'ǳuro', 'ﬃ test', "they\'re", 'ᾲ'):
    print(
        'case', [ord(char) for char in text],
        [ord(char) for char in text.lower()], [ord(char) for char in text.upper()],
        [ord(char) for char in text.title()], [ord(char) for char in text.capitalize()],
        [ord(char) for char in text.swapcase()], [ord(char) for char in text.casefold()],
        text.istitle(),
    )

normalization_text = 'A\u030A e\u0301 \u212B \uAC00 \u1100\u1161 \uFB03 \u2163 \U0001D15E'
for form in ('NFC', 'NFD', 'NFKC', 'NFKD'):
    normalized = u.normalize(form, normalization_text)
    print('normalize', form, [ord(char) for char in normalized], u.is_normalized(form, normalized))
try:
    u.normalize('nfc', 'x')
except ValueError:
    print('normalize-form-case', True)

legacy = u.ucd_3_2_0
print('legacy-version', legacy.unidata_version)
for cp in (0x0041, 0x03F9, 0x0221, 0x20AC, 0x20000, 0x2FA1D):
    char = chr(cp)
    print(
        'legacy-property', hex(cp), legacy.name(char, None), legacy.category(char),
        legacy.bidirectional(char), legacy.combining(char),
        legacy.east_asian_width(char), legacy.mirrored(char),
        legacy.decimal(char, None), legacy.digit(char, None),
        legacy.numeric(char, None), legacy.decomposition(char),
    )
for requested in ('TAMIL OM', 'GRINNING FACE', 'CJK UNIFIED IDEOGRAPH-20000'):
    print('legacy-lookup', requested, [ord(char) for char in legacy.lookup(requested)])
try:
    legacy.lookup('BYTE ORDER MARK')
except KeyError:
    print('legacy-rejects-modern-alias', True)
for cp in (0x2F868, 0x2F874, 0x2F91F, 0x2F95F, 0x2F9BF):
    char = chr(cp)
    print('legacy-correction', hex(cp), legacy.decomposition(char), [ord(c) for c in legacy.normalize('NFD', char)])

for text, trim_chars in (
    ('\u00a0x\u2003', None), ('\u1680a\u2028b\u205fc', None), ('界abc界', '界'),
):
    stripped = text.strip() if trim_chars is None else text.strip(trim_chars)
    print(
        'unicode-boundary', [ord(c) for c in stripped],
        [[ord(c) for c in part] for part in text.split()],
        [[ord(c) for c in part] for part in text.rsplit(None, 1)],
        [[ord(c) for c in part] for part in text.splitlines(True)],
    )
padding = '界a'
for width in (2, 3, 4, 5):
    print(
        'unicode-padding', width, [ord(c) for c in padding.center(width)],
        [ord(c) for c in padding.ljust(width, 'é')],
        [ord(c) for c in padding.rjust(width, '🙂')],
        [ord(c) for c in ('+' + padding).zfill(width)], padding.count(''),
    )

codec_names = sorted(set(encodings.aliases.aliases) | set(encodings.aliases.aliases.values()) | {'oem'})
missing = []
for codec_name in codec_names:
    try:
        codecs.lookup(codec_name)
    except LookupError:
        missing.append(codec_name)
print('codec-alias-catalog', len(codec_names), missing)
