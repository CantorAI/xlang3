"""Match unresolved sampled PE ranges to existing COFF code, masking relocations.

File analysis only. A match requires the entire containing .pdata code range
to match a COFF function prefix after masking only COFF relocation bytes.
All compatible symbol aliases are preserved. No target image is loaded.
"""
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct
import sys

from importlib.util import module_from_spec, spec_from_file_location


def functions(path):
    data = path.read_bytes()
    big = data[:4] == b'\0\0\xff\xff'
    if big:
        assert struct.unpack_from('<H', data, 6)[0] == 0x8664
        count, symbols, symbol_count = struct.unpack_from('<III', data, 44)
        section_table, symbol_size = 56, 20
    else:
        machine, count, _, symbols, symbol_count, optional, _ = struct.unpack_from('<HHIIIHH', data, 0)
        assert machine == 0x8664
        section_table, symbol_size = 20 + optional, 18
    strings = symbols + symbol_count * symbol_size
    names = defaultdict(list)
    index = 0
    while index < symbol_count:
        offset = symbols + index * symbol_size
        if data[offset:offset + 4] == b'\0\0\0\0':
            name_offset = strings + struct.unpack_from('<I', data, offset + 4)[0]
            name = data[name_offset:data.index(b'\0', name_offset)].decode('ascii', errors='replace')
        else:
            name = data[offset:offset + 8].split(b'\0')[0].decode('ascii', errors='replace')
        value = struct.unpack_from('<I', data, offset + 8)[0]
        section = struct.unpack_from('<i' if big else '<h', data, offset + 12)[0]
        kind = struct.unpack_from('<H', data, offset + (16 if big else 14))[0]
        auxiliary = data[offset + (19 if big else 17)]
        if section > 0 and kind & 0x20:
            names[(section, value)].append(name)
        index += 1 + auxiliary
    for section in range(1, count + 1):
        offset = section_table + (section - 1) * 40
        size, raw, relocs = struct.unpack_from('<III', data, offset + 16)
        relocation_count = struct.unpack_from('<H', data, offset + 32)[0]
        characteristics = struct.unpack_from('<I', data, offset + 36)[0]
        if not characteristics & 0x20 or not raw:
            continue
        if characteristics & 0x1000000:
            raise ValueError('Relocation overflow unsupported')
        relocation_rows = []
        for i in range(relocation_count):
            position, _, kind = struct.unpack_from('<IIH', data, relocs + i * 10)
            # 0x0b is IMAGE_REL_AMD64_SECREL, a 32-bit section-relative field
            # (including TLS data references), not an instruction byte.
            width = 8 if kind == 1 else (4 if kind in (2, 3, 4, 5, 6, 7, 8, 9, 11) else (2 if kind == 10 else None))
            if width is None and kind != 0:
                raise ValueError('Unsupported AMD64 relocation ' + hex(kind))
            if width:
                relocation_rows.append((position, width))
        for (symbol_section, value), aliases in names.items():
            if symbol_section == section and value < size:
                yield aliases, data[raw + value:raw + size], [(position - value, width)
                    for position, width in relocation_rows if position >= value], section, value


def main():
    assert sys.version_info[:3] == (3, 14, 7)
    root = Path(r'D:/CantorAI/xlang3')
    analysis_path = root / 'scratch/performance/sqlglot-parse-body-native-export-ranges-20261008.json'
    output = root / 'scratch/performance/sqlglot-parse-body-native-coff-matches-20261008.json'
    assert not output.exists(), 'Preserve earlier analysis'
    analysis = json.loads(analysis_path.read_text(encoding='utf-8'))
    spec = spec_from_file_location('pe_reader', root / 'scratch/performance/attribute-sqlglot-native-exports-20261008.py')
    helper = module_from_spec(spec)
    spec.loader.exec_module(helper)
    image_path = Path(analysis['image'])
    if not image_path.is_absolute():
        image_path = root / image_path
    assert helper.digest(image_path) == analysis['image_sha256']
    pe = helper.PE(image_path)
    decode = helper.demangler()
    targets = {}
    for row in analysis['runtime_rows']:
        if row['confidence'].startswith('unresolved') and row['containing_pdata']:
            begin, end, unwind = (int(value, 16) for value in row['containing_pdata'])
            targets.setdefault(begin, {'end': end, 'points': 0, 'matches': []})['points'] += row['samples']
    # These unchanged translation units cover the four leading private ranges.
    objects = [root / ('build-repro/main-verify-20261006/CMakeFiles/xlang3_runtime.dir/Release/' + file)
               for file in ('src/runtime/object_model.cpp.obj',
                            'src/executor/xlang_vm/xlang_vm_loop.cpp.obj',
                            'src/executor/xlang_vm/xlang_vm_attr.cpp.obj')]
    snapshots = {str(path): helper.digest(path) for path in objects}
    for path in objects:
        for aliases, object_code, relocations, section, value in functions(path):
            for begin, target in targets.items():
                size = target['end'] - begin
                if size < 32 or size > len(object_code):
                    continue
                image_code = bytearray(pe.data[pe.offset(begin):pe.offset(begin) + size])
                candidate = bytearray(object_code[:size])
                for position, width in relocations:
                    if position < size:
                        mask = min(width, size - position)
                        image_code[position:position + mask] = b'\0' * mask
                        candidate[position:position + mask] = b'\0' * mask
                if candidate == image_code:
                    target['matches'].append({'object': str(path), 'section': section,
                        'symbol_section_offset': value, 'code_bytes_matched': size,
                        'relocation_masked_bytes': sum(min(width, size - position) for position, width in relocations if position < size),
                        'aliases': [decode(name) for name in aliases]})
    assert snapshots == {str(path): helper.digest(path) for path in objects}, 'Object changed during analysis'
    record = {'purpose': 'COFF relocation-masked full pdata range prefix match; file analysis only',
        'analysis_sha256': helper.digest(analysis_path), 'image_sha256': analysis['image_sha256'],
        'script_sha256': helper.digest(Path(__file__)), 'objects_sha256': snapshots,
        'ranges': [{'begin': hex(begin), **target} for begin, target in sorted(targets.items(), key=lambda pair: pair[1]['points'], reverse=True)],
        'limits': ['Existing object files are not assumed to identify the whole linked image',
                   'Only full code-range prefix byte matches after listed relocation masking are used',
                   'ICF/coalesced or prefix-equivalent aliases remain ambiguous; retain every match',
                   'Unmatched private ranges remain unresolved; no nearby-export naming'],
        'analysis_python': sys.version}
    output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps([row for row in record['ranges'] if row['matches']][:15]))


if __name__ == '__main__':
    main()
