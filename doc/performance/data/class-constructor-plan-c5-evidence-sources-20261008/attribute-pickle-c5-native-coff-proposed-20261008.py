"""Held exact C5 pickle private-range attribution; file analysis, no target loading.

Reuse the reviewed COFF decoder; match complete containing PE code ranges after
masking only explicit object relocation fields. Preserve all ambiguous aliases.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
PE_HELPER = ROOT / 'scratch/performance/attribute-sqlglot-native-exports-20261008.py'
COFF_HELPER = ROOT / 'scratch/performance/match-sqlglot-native-coff-20261008.py'
IMAGE = ROOT / 'build-repro/main-verify-20261006/Release/xlang3_runtime.dll'
OBJ_ROOT = ROOT / 'build-repro/main-verify-20261006/CMakeFiles/xlang3_runtime.dir/Release'
OBJECTS = [OBJ_ROOT / p for p in ('src/executor/xlang_vm/xlang_vm_loop.cpp.obj',
    'src/executor/xlang_vm/xlang_interpreter.cpp.obj', 'src/executor/xlang_vm/xlang_vm_attr.cpp.obj',
    'src/runtime/object_model.cpp.obj', 'src/builtins/functional_builtins.cpp.obj',
    'src/runtime/functional_iterators.cpp.obj', 'src/runtime/sequence.cpp.obj', 'src/runtime/mapping.cpp.obj',
    'src/runtime/module_object.cpp.obj', 'src/runtime/value.cpp.obj', 'src/runtime/runtime.cpp.obj')]
OBJECT_HASHES = {
    'src/executor/xlang_vm/xlang_vm_loop.cpp.obj': '6cc8b010afbb6f19002a1af0628f32f82bc194787bfbf3add00f10aee08c01d7',
    'src/executor/xlang_vm/xlang_interpreter.cpp.obj': '01cb9d741c26f42349f7944b02d5d61d87ae9fb3dbd01717413072c79664ce5a',
    'src/executor/xlang_vm/xlang_vm_attr.cpp.obj': '8ec9d2d5b5024e6c60ab4bef0ffdca18ebe795114700ebd58fe5439cad266b2e',
    'src/runtime/object_model.cpp.obj': '1252badaeaf2b7bcc7a7c0c169db18b1a9447809f59033297a7182c7f05c58cb',
    'src/builtins/functional_builtins.cpp.obj': 'f57d656c9556c82e36617fe9e62c221732cee848116a3cbf7d531b3b9ca78450',
    'src/runtime/functional_iterators.cpp.obj': 'c4494fa6dd8d44f1c485d58a4a355a60b21905ba76cfe66ce27fff86a25e0d3e',
    'src/runtime/sequence.cpp.obj': '40d06d2a50eb21e85f26c5234e158a8603727d9e16e78292fcb073232f736994',
    'src/runtime/mapping.cpp.obj': 'ca73e992db202639e3a29b1d1c62d4a526fffff16efd68cb73493c4ffaaf0cba',
    'src/runtime/module_object.cpp.obj': 'd55379b594b461c31b15a7f68cee50357b7a88a0e928993c1a36b4eb0573e8dc',
    'src/runtime/value.cpp.obj': '3683534197be3912e1f2de62751532c0e027275cac97cdf919cfc1e141cfeca9',
    'src/runtime/runtime.cpp.obj': '73babc2080259c256aa1a594219f508dbc988fddf7583f6b4fe2f922d771279e',
}
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sampling-receipt', type=Path, required=True)
    parser.add_argument('--sampling-receipt-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--max-ranges', type=int, default=16)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3,14,7) and sys.flags.optimize == 0
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert 1 <= args.max_ranges <= 24 and not args.output.exists()
    assert args.sampling_receipt_sha256 == 'e5919bc0ec2840d3300888f1fb54dd10ac66cb96a66734d47a05322d26f5a0a7'
    assert sha(args.sampling_receipt) == args.sampling_receipt_sha256
    assert sha(PE_HELPER) == 'e535c5d629e5fb4bfc1a360f7b730df0a9d13de86c6231c92e39244b1aa7920e'
    assert sha(COFF_HELPER) == '796b3cf6860c428f0dd76f36d30bcc2a6d9cf958f15fef1467e8271047e58411'
    sampled = json.loads(args.sampling_receipt.read_bytes())
    assert sampled['terminal'] and sampled['hashes_unchanged'] and sampled['diagnostic_only'] and not sampled['acceptance']
    assert sampled['status'] == 'terminal_unscored_native_location_diagnostic'
    assert sha(IMAGE) == sampled['candidate_binary_sha256']['dll']
    assert sampled['mode'] == 'sample' and not sampled['scored'] and not sampled['full_validated']
    assert sampled['outer_loops'] == 41 and sampled['original_dump_operations'] == 2460 and sampled['protocol'] == 5
    assert len(sampled['raw']) == 1 and sampled['raw'][0]['passed'] and sampled['raw'][0]['exit_code'] == 0
    stream = sampled['raw'][0]
    assert Path(stream['samples_log']).name == stream['samples_log']
    raw_path = ROOT / 'doc/performance/data' / stream['samples_log']
    assert sha(raw_path) == stream['samples_sha256'] == 'd2d93d48eec095765066cc53af03cc14340a2b2b83bd2b6852c36adacac2ee88'
    inventory_path = Path(sampled['source_inventory'])
    assert sha(inventory_path) == sampled['source_inventory_sha256'] == '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937'
    inventory = json.loads(inventory_path.read_bytes())
    assert sampled['source_sha256'] == inventory['source_sha256'] and len(sampled['source_sha256']) == 106
    body_path = Path(sampled['body_reference']['path'])
    assert sha(body_path) == sampled['body_reference']['sha256'] == '2d6a79f263d3e20f86f494bc13dfef795e395fba0b81773a2bc238472d811665'
    body = json.loads(body_path.read_bytes())
    assert body['terminal'] and body['hashes_unchanged'] and body['status'] == 'terminal_unscored_original_body_match'
    assert body['source_sha256'] == sampled['source_sha256'] and body['binaries_sha256'] == sampled['binaries_sha256']
    for relative, expected in sampled['source_sha256'].items(): assert sha(ROOT / relative) == expected
    for path in OBJECTS: assert sha(path) == OBJECT_HASHES[path.relative_to(OBJ_ROOT).as_posix()]
    assert str(IMAGE.resolve()) in sampled['hashes_before'] and sampled['hashes_before'][str(IMAGE.resolve())] == sha(IMAGE)
    before = {str(p):sha(p) for p in [IMAGE, raw_path, inventory_path, body_path, args.sampling_receipt, PE_HELPER, COFF_HELPER, Path(__file__), *OBJECTS,
        *(ROOT / relative for relative in sampled['source_sha256'])]}
    pe_helpers, coff_helpers = load('pickle_pe', PE_HELPER), load('pickle_coff', COFF_HELPER)
    image = pe_helpers.PE(IMAGE); decode = pe_helpers.demangler()
    rows = [json.loads(line) for line in raw_path.read_text(encoding='utf-8').splitlines() if line.strip()]
    runtime_modules = {(m['base'],m['size']):m for row in rows if row['event'] == 'modules'
        for m in row['items'] if Path(m['path']).resolve() == IMAGE.resolve()}
    assert len(runtime_modules) == 1
    module = next(iter(runtime_modules.values())); base, size = module['base'], module['size']
    samples = [r for r in rows if r['event'] == 'sample']
    terminal = [r for r in rows if r['event'] == 'terminal']
    assert len(terminal) == 1 and rows[-1] == terminal[0] and terminal[0] == sampled['sampler_terminal']
    assert len(samples) == sampled['sample_count'] == terminal[0]['sample_count'] == 190
    exclusive, inclusive, targets = Counter(), Counter(), {}
    for sample in samples:
        this_stack = set()
        for depth, raw_pc in enumerate(sample['pcs']):
            # The leaf is an actual instruction pointer; deeper unwind entries
            # are return PCs, so use PC-1 to locate their call-site function.
            pc = raw_pc if depth == 0 else raw_pc - 1
            if not base <= pc < base + size: continue
            rva = pc - base; function = image.containing(rva)
            if not function: continue
            begin, end, unwind = function
            chain = image.chain(function)
            if any(image.export_names_in(f) for f in chain) or rva in image.exports: continue
            targets.setdefault(begin, dict(begin=begin, end=end, unwind=unwind, matches=[]))
            if depth == 0: exclusive[begin] += 1
            this_stack.add(begin)
        inclusive.update(this_stack)
    # Prioritize actual self ranges. Fill the remainder with inclusive callers
    # to identify dispatch paths, keeping overlapping stacks strictly separate.
    leaf_ranked = sorted((b for b in targets if exclusive[b]), key=lambda b:(exclusive[b],inclusive[b]), reverse=True)
    chosen = {b:targets[b] for b in leaf_ranked[:min(12,args.max_ranges)]}
    for begin in sorted(targets, key=lambda b:(inclusive[b],exclusive[b]), reverse=True):
        if len(chosen) >= args.max_ranges: break
        chosen.setdefault(begin,targets[begin])
    assert len(chosen) <= args.max_ranges <= 24
    raw_code = {b:image.data[image.offset(b):image.offset(b) + target['end'] - b] for b,target in chosen.items()}
    tested_functions = 0
    for path in OBJECTS:
        for aliases, object_code, relocations, section, value in coff_helpers.functions(path):
            tested_functions += 1
            for begin, target in chosen.items():
                length = target['end'] - begin
                if length < 32 or length > len(object_code): continue
                # Cheap early rejection, followed by the same full-prefix proof.
                prefix = min(16,length)
                left, right = bytearray(raw_code[begin][:prefix]), bytearray(object_code[:prefix])
                for position,width in relocations:
                    if position < prefix:
                        amount = min(width,prefix-position); left[position:position+amount] = right[position:position+amount] = b'\0' * amount
                if left != right: continue
                left, right = bytearray(raw_code[begin]), bytearray(object_code[:length])
                masked = set()
                for position,width in relocations:
                    if position < length:
                        amount = min(width,length-position); left[position:position+amount] = right[position:position+amount] = b'\0' * amount
                        masked.update(range(position,position+amount))
                if left == right:
                    target['matches'].append(dict(object=str(path), object_sha256=before[str(path)], section=section,
                        symbol_section_offset=value, code_bytes_matched=length, relocation_masked_bytes=len(masked),
                        aliases=[decode(name) for name in aliases], decorated_aliases=aliases))
    after = {p:sha(p) for p in before}
    assert before == after, 'Analysis input changed'
    result = dict(status='terminal_file_only_coff_attribution', terminal=True, diagnostic_only=True,
        runtime_execution=False, engine_changes=False, source_loading=False, image_sha256=before[str(IMAGE)],
        source_inventory_sha256=sampled['source_inventory_sha256'], body_receipt_sha256=sampled['body_reference']['sha256'],
        reviewed_base_script_sha256='04ec10cc996243c493f63f3e6c9853898c61d2e35c07b6abc86a5af7c2119d2f',
        sampling_receipt=str(args.sampling_receipt), sampling_receipt_sha256=args.sampling_receipt_sha256,
        sample_count=len(samples), return_pc_minus_one=True, chosen_private_ranges=len(chosen),
        tested_object_functions=tested_functions, hashes_before=before, hashes_after=after, hashes_unchanged=True,
        ranges=[dict(target, begin_hex=hex(begin), end_hex=hex(target['end']),
            exclusive_leaf_samples=exclusive[begin], exclusive_fraction_all=exclusive[begin]/len(samples),
            inclusive_stack_samples=inclusive[begin], inclusive_fraction_all=inclusive[begin]/len(samples))
            for begin,target in chosen.items()],
        limits=['Native suspensions and GetThreadTimes selection affect sampling distribution; no score',
            'Full containing range/prefix equality is proved only after explicit relocation masking',
            'Prefix-equivalent/ICF aliases are retained; a match does not prove relocation target identity',
            'Inclusive ranges overlap and are not additive self CPU',
            'Only eleven exact current runtime objects checked; other unmatched ranges remain unresolved',
            'Body-end includes three post-body dump/load audits; no sample filtering or timing score'])
    args.output.write_bytes((json.dumps(result,indent=2)+'\n').encode('utf-8'))
    print(json.dumps([dict(range=t['begin_hex'], leaf=t['exclusive_leaf_samples'], inclusive=t['inclusive_stack_samples'],
        matches=[dict(aliases=m['aliases'], bytes=m['code_bytes_matched'], masked=m['relocation_masked_bytes'],
        object=Path(m['object']).name) for m in t['matches']]) for t in result['ranges']], indent=2))
    return 0

if __name__ == '__main__': raise SystemExit(main())
