"""Refresh pinned native error metadata from the CPython 3.14 oracle.

This development-only script is not part of XLang3's build or runtime.
Run it from the repository root with pydantic-core 2.46.5 on PYTHONPATH.
"""

import json
import re
from pathlib import Path

from pydantic_core import __version__
from pydantic_core._pydantic_core import list_all_errors


rows = list_all_errors()
assert __version__ == '2.46.5', __version__
assert len(rows) == 104
data = json.dumps(rows, ensure_ascii=True, separators=(',', ':'))
source = Path('scratch/upstream-compat/pydantic_core-2.46.5/src/errors/types.rs').read_text(encoding='utf-8')
definitions = source.split('error_types! {', 1)[1].split('\n}\n\nmacro_rules! render', 1)[0]
requirements = []
for match in re.finditer(r'^\s*([A-Z]\w+)\s*\{', definitions, re.M):
    enum_name = match.group(1)
    depth = 1
    end = match.end()
    while depth:
        depth += (definitions[end] == '{') - (definitions[end] == '}')
        end += 1
    body = definitions[match.end():end - 1]
    type_name = re.sub(r'(?<!^)(?=[A-Z])', '_', enum_name).lower()
    for field_match in re.finditer(r'(\w+):\s*\{ctx_type:\s*(.*?),\s*ctx_fn:', body, re.S):
        field_name, field_type = field_match.groups()
        if enum_name != 'CustomError':
            requirements.append((type_name, enum_name, field_name, field_type.strip()))
assert len(requirements) >= 50, len(requirements)
assert {item[0] for item in requirements} <= {row['type'] for row in rows}
output = Path('modules/pydantic_core/pydantic_error_catalog.inc')
output.write_text(
    '// Pinned pydantic-core 2.46.5 list_all_errors() metadata, verified with CPython 3.14.\n'
    'constexpr const char kPydanticErrorCatalogJson[] = R"X3ERR('
    + data
    + ')X3ERR";\n'
    + 'struct PydanticErrorTemplate { const char* type; const char* message; const char* json_message; };\n'
    + 'constexpr PydanticErrorTemplate kPydanticErrorTemplates[] = {\n'
    + ''.join(
        '  {' + json.dumps(row['type'], ensure_ascii=True) + ', '
        + json.dumps(row['message_template_python'], ensure_ascii=True) + ', '
        + json.dumps(row.get('message_template_json', row['message_template_python']), ensure_ascii=True) + '},\n'
        for row in rows
    )
    + '};\n'
    + 'struct PydanticContextRequirement { const char* type; const char* enum_name; const char* field; const char* expected; };\n'
    + 'constexpr PydanticContextRequirement kPydanticContextRequirements[] = {\n'
    + ''.join(
        '  {' + ', '.join(json.dumps(part, ensure_ascii=True) for part in requirement) + '},\n'
        for requirement in requirements
    )
    + '};\n',
    encoding='utf-8',
    newline='\n',
)
print(len(rows), len(data), len(requirements), output)
