"""Read-only PE export/.pdata attribution of the retained SQLGlot native sample.

Does not load/execute the target image. Only exact export entries within the
containing .pdata range, or an explicit UNW_FLAG_CHAININFO parent, identify a
function. Nearest preceding exports outside that range are hints, never labels.
"""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class PE:
    def __init__(self, path):
        self.data = path.read_bytes()
        assert self.data[:2] == b'MZ'
        pe = self.u32(0x3c)
        assert self.data[pe:pe + 4] == b'PE\0\0' and self.u16(pe + 4) == 0x8664
        optional = pe + 24
        assert self.u16(optional) == 0x20b, 'Require AMD64 PE32+'
        self.directories = [struct.unpack_from('<II', self.data, optional + 112 + i * 8)
                            for i in range(min(self.u32(optional + 108), 16))]
        section_table = optional + self.u16(pe + 20)
        self.sections = []
        for i in range(self.u16(pe + 6)):
            row = section_table + i * 40
            virtual_size, rva, raw_size, offset = struct.unpack_from('<IIII', self.data, row + 8)
            self.sections.append((rva, max(virtual_size, raw_size), offset, raw_size))
        self.exports = self.read_exports()
        rva, size = self.directories[3]
        assert size % 12 == 0, 'Exception directory is not RUNTIME_FUNCTION records'
        offset = self.offset(rva)
        self.ranges = sorted(struct.unpack_from('<III', self.data, offset + i)
                             for i in range(0, size, 12))
        assert all(begin < end for begin, end, unwind in self.ranges)
        self.begins = [row[0] for row in self.ranges]
        self.export_entries = sorted(self.exports)

    def u16(self, offset):
        return struct.unpack_from('<H', self.data, offset)[0]

    def u32(self, offset):
        return struct.unpack_from('<I', self.data, offset)[0]

    def offset(self, rva):
        for begin, size, offset, raw_size in self.sections:
            if begin <= rva < begin + size:
                assert rva - begin < raw_size, 'RVA maps to zero-fill rather than file bytes'
                return offset + rva - begin
        raise ValueError('Unmapped RVA ' + hex(rva))

    def cstring(self, rva):
        offset = self.offset(rva)
        end = self.data.find(b'\0', offset, offset + 65536)
        assert end >= offset
        return self.data[offset:end].decode('ascii', errors='replace')

    def read_exports(self):
        rva, size = self.directories[0]
        offset = self.offset(rva)
        functions, names = self.u32(offset + 20), self.u32(offset + 24)
        eat = self.offset(self.u32(offset + 28))
        name_table = self.offset(self.u32(offset + 32))
        ordinals = self.offset(self.u32(offset + 36))
        result = defaultdict(list)
        for i in range(names):
            ordinal = self.u16(ordinals + i * 2)
            assert ordinal < functions
            entry = self.u32(eat + ordinal * 4)
            if entry == 0 or rva <= entry < rva + size:
                continue  # Null entry or forwarded export, not an image code address.
            result[entry].append(self.cstring(self.u32(name_table + i * 4)))
        return dict(result)

    def containing(self, rva):
        index = bisect_right(self.begins, rva) - 1
        if index >= 0 and rva < self.ranges[index][1]:
            return self.ranges[index]
        return None

    def chain(self, function):
        result = [function]
        for unused in range(32):
            unwind = self.offset(result[-1][2] & ~1)
            version_flags, _, count, _ = struct.unpack_from('<BBBB', self.data, unwind)
            version, flags = version_flags & 7, version_flags >> 3
            assert version in (1, 2), 'Unsupported unwind metadata version'
            if not flags & 4:
                return result
            assert not flags & 3, 'Chained unwind also specifies an exception handler'
            chained_offset = unwind + 4 + ((count + 1) & ~1) * 2
            chained = struct.unpack_from('<III', self.data, chained_offset)
            assert chained not in result and chained[0] < chained[1], 'Invalid unwind chain'
            result.append(chained)
        raise ValueError('Unwind chain too long')

    def export_names_in(self, function):
        begin, end, unused = function
        return [(entry, name) for entry in self.export_entries if begin <= entry < end
                for name in self.exports[entry]]


def demangler():
    # This OS API formats exported names only; it never loads the target DLL.
    library = ctypes.WinDLL(str(Path(os.environ.get('SystemRoot', 'C:/Windows')) /
                                'System32/dbghelp.dll'))
    function = library.UnDecorateSymbolName
    function.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32)
    function.restype = ctypes.c_uint32

    def decode(name):
        result = ctypes.create_string_buffer(32768)
        return result.value.decode('utf-8', errors='replace') if function(
            name.encode('ascii', errors='replace'), result, len(result), 0) else name
    return decode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--samples', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--binary-provenance', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--readable', type=Path, required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7), 'Require CPython 3.14.7 for analysis'
    assert not args.output.exists() and not args.readable.exists(), 'Preserve previous analysis'
    snapshot = json.loads(args.binary_provenance.read_text(encoding='utf-8-sig'))
    image_sha256 = digest(args.image)
    expected = next(row['Hash'].lower() for row in snapshot
                    if Path(row['Path']).name.lower() == args.image.name.lower())
    assert image_sha256 == expected, 'Image no longer matches sampled binary'
    samples = json.loads(args.samples.read_text(encoding='utf-8-sig'))
    assert samples['return_code'] == 0 and samples['start_marker_seen'] and samples['end_marker_seen']
    image = PE(args.image)
    decode = demangler()
    decoded = {name: decode(name) for aliases in image.exports.values() for name in aliases}
    groups = Counter()
    group_names = {}
    rows = []
    runtime_samples = 0
    for sample in samples['samples']:
        if Path(sample['module']).name.lower() != args.image.name.lower():
            continue
        runtime_samples += sample['samples']
        rva = int(sample['rva'], 16)
        function = image.containing(rva)
        chain = image.chain(function) if function is not None else []
        anchored = []
        anchor_function = None
        for candidate in chain:
            anchored = image.export_names_in(candidate)
            if anchored:
                anchor_function = candidate
                break
        if anchored:
            key = 'export-range:' + hex(anchor_function[0])
            group_names[key] = sorted(set(decoded[name] for entry, name in anchored))
            confidence = 'export_in_containing_pdata_range' if anchor_function == function else 'explicit_chained_unwind_export_range'
        elif rva in image.exports:
            key = 'exact-export:' + hex(rva)
            group_names[key] = sorted(set(decoded[name] for name in image.exports[rva]))
            confidence = 'exact_export_entry_without_pdata'
        else:
            key = 'unresolved-range:' + (hex(function[0]) if function is not None else hex(rva))
            group_names[key] = []
            confidence = 'unresolved_no_export_in_pdata_range'
        groups[key] += sample['samples']
        prior = bisect_right(image.export_entries, rva) - 1
        hint = None
        if prior >= 0:
            entry = image.export_entries[prior]
            hint = {'entry': hex(entry), 'distance': rva - entry,
                    'names': [decoded[name] for name in image.exports[entry]]}
        rows.append({**sample, 'group': key, 'confidence': confidence,
            'containing_pdata': [hex(value) for value in function] if function is not None else None,
            'unwind_chain': [[hex(value) for value in value_range] for value_range in chain],
            'anchored_exports': [{'entry': hex(entry), 'name': decoded[name], 'decorated': name}
                                 for entry, name in anchored],
            'nearest_export_hint_only': hint})
    summaries = [{'group': key, 'samples': count, 'fraction_runtime': count / runtime_samples,
                  'fraction_all': count / samples['sample_count'], 'exports': group_names[key]}
                 for key, count in groups.most_common()]
    resolved = sum(row['samples'] for row in rows if not row['confidence'].startswith('unresolved'))
    record = {'purpose': 'PE export/.pdata file-only native attribution; no workload run',
        'analysis_python': sys.version, 'image': str(args.image), 'image_sha256': image_sha256,
        'samples_source': str(args.samples), 'samples_sha256': digest(args.samples),
        'script_sha256': digest(Path(__file__)), 'all_samples': samples['sample_count'],
        'runtime_samples': runtime_samples, 'export_range_resolved_samples': resolved,
        'unresolved_runtime_samples': runtime_samples - resolved,
        'named_export_addresses': len(image.exports), 'pdata_ranges': len(image.ranges),
        'groups': summaries, 'runtime_rows': rows,
        'limits': ['IP samples are self-location diagnostics, not timings or call stacks',
                   'Export range can contain inline functions or coalesced aliases',
                   'Nearest export outside containing range is never an attribution',
                   'Private/inlined source identity requires matching symbols or disassembly'],
        'unwind_format_reference': 'https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170'}
    args.output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    lines = ['# SQLGlot body native export-range attribution', '',
        'File-only analysis with exact CPython 3.14.7; no XLang3 execution or rebuild.', '',
        f"Total points: {samples['sample_count']}; runtime points: {runtime_samples}; "
        f"resolved by export entry/range or explicit unwind chain: {resolved}; unresolved: {runtime_samples - resolved}.", '',
        '| Points | % runtime | Export/range |', '| ---: | ---: | --- |']
    for row in summaries[:25]:
        names = '<br>'.join(name.replace('|', '\\|') for name in row['exports'][:3]) or row['group']
        lines.append(f"| {row['samples']} | {100 * row['fraction_runtime']:.2f}% | {names} |")
    lines += ['', 'Names identify the containing compiled function/range, including inlined work; '
        'these points do not prove which inline operation dominates. Coalesced aliases remain ambiguous. '
        'Unresolved ranges and nearest-export hints remain preserved in JSON; no distant export is assigned.', '',
        '[Microsoft x64 unwind format](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170)', '']
    args.readable.write_text('\n'.join(lines), encoding='utf-8')
    print(json.dumps({'runtime_points': runtime_samples, 'resolved': resolved,
                      'unresolved': runtime_samples - resolved, 'top_groups': summaries[:12]}))


if __name__ == '__main__':
    main()
