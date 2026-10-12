"""File-only admission before the unchanged native COFF function reader.

Windows SDK ANON_OBJECT_HEADER is not BigObj merely because Sig1/Sig2 match.
Anonymous v1 MSVC /GL payloads are opaque and produce no native-function rows.
This facade admits only bounded classic AMD64 COFF; other formats stay explicit.
"""
from pathlib import Path
import struct

CL_GL_CLASS_ID = bytes.fromhex('38feb30ca5d9ab4dac9bd6b6222653c2')
BIGOBJ_CLASS_ID = bytes.fromhex('c7a1bad1eebaa94baf20faf66aa4dcb8')


def inspect_object(path):
    path = Path(path)
    data = path.read_bytes()
    report = dict(path=str(path), size=len(data), header_first_32_hex=data[:32].hex(),
                  supported=False, format='unsupported', reason=None)

    def bounded(offset, size, label):
        if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
            raise ValueError('Out-of-file ' + label)

    try:
        bounded(0, 8, 'header signature')
        if data[:4] == b'\0\0\xff\xff':
            version, machine = struct.unpack_from('<HH', data, 4)
            report.update(version=version, machine=machine)
            bounded(0, 32, 'anonymous header')
            class_id = data[12:28]
            report['class_id_hex'] = class_id.hex()
            report['size_of_data'] = struct.unpack_from('<I', data, 28)[0]
            if version == 1 and class_id == CL_GL_CLASS_ID:
                report.update(format='msvc_cl_gl_anonymous_v1',
                    reason='Unsupported opaque MSVC /GL intermediate object; not native COFF/BigObj')
            elif version >= 2 and class_id == BIGOBJ_CLASS_ID:
                report.update(format='bigobj', reason='BigObj not admitted by this bounded classic-only facade')
            else:
                report.update(format='other_anonymous', reason='Unsupported anonymous object version/ClassID')
            return report
        bounded(0, 20, 'classic COFF header')
        machine, count, _, symbols, symbol_count, optional, _ = struct.unpack_from('<HHIIIHH', data, 0)
        report.update(format='classic_coff', machine=machine, section_count=count, symbol_count=symbol_count)
        if machine != 0x8664:
            report['reason'] = 'Unsupported non-AMD64 classic COFF'; return report
        section_table = 20 + optional
        bounded(section_table, count * 40, 'section table')
        if symbol_count:
            if symbols == 0: raise ValueError('Missing symbol table')
            bounded(symbols, symbol_count * 18, 'symbol table')
            strings = symbols + symbol_count * 18
            bounded(strings, 4, 'string table length')
            string_size = struct.unpack_from('<I', data, strings)[0]
            if string_size < 4: raise ValueError('Invalid string table length')
            bounded(strings, string_size, 'string table')
            index = 0
            while index < symbol_count:
                offset = symbols + index * 18
                auxiliary = data[offset + 17]
                if index + 1 + auxiliary > symbol_count: raise ValueError('Auxiliary symbol count exceeds table')
                if data[offset:offset + 4] == b'\0\0\0\0':
                    name = struct.unpack_from('<I', data, offset + 4)[0]
                    if name < 4 or name >= string_size: raise ValueError('Symbol name outside string table')
                    if data.find(b'\0', strings + name, strings + string_size) < 0:
                        raise ValueError('Unterminated symbol name')
                index += 1 + auxiliary
        widths = {1: 8, 2: 4, 3: 4, 4: 4, 5: 4, 6: 4, 7: 4, 8: 4, 9: 4, 10: 2, 11: 4}
        for section in range(count):
            offset = section_table + section * 40
            size, raw, relocations = struct.unpack_from('<III', data, offset + 16)
            relocation_count = struct.unpack_from('<H', data, offset + 32)[0]
            characteristics = struct.unpack_from('<I', data, offset + 36)[0]
            if raw: bounded(raw, size, 'section data')
            if not characteristics & 0x20 or not raw: continue
            if characteristics & 0x01000000: raise ValueError('Code relocation overflow unsupported by unchanged reader')
            bounded(relocations, relocation_count * 10, 'code relocation table')
            for index in range(relocation_count):
                position, _, kind = struct.unpack_from('<IIH', data, relocations + index * 10)
                if kind == 0: continue
                if kind not in widths: raise ValueError('Unsupported AMD64 code relocation ' + hex(kind))
                if position > size or widths[kind] > size - position:
                    raise ValueError('Code relocation outside section')
        report.update(supported=True, format='classic_amd64_coff', reason=None)
    except (ValueError, struct.error) as error:
        report.update(supported=False, reason=type(error).__name__ + ': ' + str(error))
    return report
