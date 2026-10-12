"""File-only attribution of every location in the fresh accepted native UTF-8 pickle sample.

No target image is loaded or executed. Reuse the existing classic COFF reader,
format admission and entire containing-range prefix match with explicit COFF
relocations masked. Aliases, unmatched ranges and unsupported objects stay raw.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
IMAGE = RELEASE / 'xlang3_runtime.dll'
BASELINE = ROOT / 'build-repro/Release'
RECEIPT = DATA / 'pickle-native-utf8-postfix-native-sampling-r2-20261008.json'
RECEIPT_SHA = '4c627d679d44d9d89b8dd8799f52a6ff1dfbba3356d7dde4f81c8f6b0f99a4fa'
OUTPUT = DATA / 'pickle-native-utf8-postfix-native-coff-attribution-20261008.json'
INVENTORY = DATA / 'native-str-utf8-applied-source-20261008.json'
INVENTORY_SHA = '96ba3820206e00029a26231b15accb79ff94e1ba33098d769e586d7a28b12f83'
INPUTS = ROOT / 'scratch/performance/pickle-native-utf8-postfix-native-sampling-inputs-proposed-20261008.json'
INPUTS_SHA = 'a8be77e20bab1a752a870872e194ecabe3354626398916d6e155510002b282d5'
CAPTURE = ROOT / 'scratch/performance/run-pickle-native-utf8-postfix-native-sampling-r2-root-20261008.py'
CAPTURE_SHA = '4896ba36ad798c43df902621b70648969e74a8704bbfb18711fd268d24e05c23'
PE_HELPER = ROOT / 'scratch/performance/attribute-sqlglot-native-exports-20261008.py'
PE_HELPER_SHA = 'e535c5d629e5fb4bfc1a360f7b730df0a9d13de86c6231c92e39244b1aa7920e'
COFF_HELPER = ROOT / 'scratch/performance/match-sqlglot-native-coff-20261008.py'
COFF_HELPER_SHA = '796b3cf6860c428f0dd76f36d30bcc2a6d9cf958f15fef1467e8271047e58411'
FORMAT_HELPER = ROOT / 'scratch/performance/inspect-r4-native-coff-format-proposed-20261008.py'
FORMAT_HELPER_SHA = '2b78c2f18f53c8364caab72368a4717c24a8396cad21d9a9dda687585c60f607'
IMAGE_SHA = '64640e503a103341dd44d220eba17ef802219b9df639bb1be5cbe5f2bf847564'
CHILD_SHA = '5295c899d95d023ca2fb3b511e9bf78b58cb49e4a256770c65adf0d614b14109'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
tree = lambda directory: {p.relative_to(directory).as_posix(): sha(p) for p in sorted(directory.rglob('*')) if p.is_file()}


def load(name,path):
    spec = importlib.util.spec_from_file_location(name,path)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sampling-receipt',type=Path,default=RECEIPT)
    parser.add_argument('--sampling-receipt-sha256',default=RECEIPT_SHA)
    parser.add_argument('--output',type=Path,default=OUTPUT)
    parser.add_argument('--max-ranges',type=int,default=4096)
    args = parser.parse_args()
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3,14,7) or sys.flags.optimize:
        raise RuntimeError('Only unoptimized CPython 3.14.7 may perform this file analysis')
    if Path(sys.executable).resolve() != CP.resolve(): raise RuntimeError('Use the fixed CPython 3.14.7')
    assert args.sampling_receipt.resolve() == RECEIPT.resolve() and args.sampling_receipt_sha256 == RECEIPT_SHA
    assert args.output.resolve() == OUTPUT.resolve() and not args.output.exists()
    assert 1 <= args.max_ranges <= 4096
    pins,objects,release,baseline = {},{}, {},{}
    record = dict(status='preflight_file_only_attribution',terminal=False,diagnostic_only=True,
        scored=False,target_image_loaded=False,runtime_execution=False,engine_changes=False,
        started_utc=datetime.now(timezone.utc).isoformat(),max_ranges=args.max_ranges,
        sampling_receipt=str(RECEIPT),sampling_receipt_sha256=RECEIPT_SHA)

    def pin(path,expected=None):
        path = Path(path).resolve(strict=True); value = sha(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value; return value
    def release_map(): return {p.relative_to(ROOT).as_posix():sha(p) for p in sorted(RELEASE.rglob('*')) if p.is_file()}
    def current_objects():
        result = {}
        for relative in objects['object_roots']:
            root = (ROOT/relative).resolve(strict=True)
            for path in sorted(root.rglob('*.obj')):
                if not path.is_file(): continue
                assert not path.is_symlink() and path.resolve().is_relative_to(root)
                key = path.relative_to(ROOT).as_posix(); assert key not in result
                result[key] = sha(path)
        return result
    def log_path(name):
        assert Path(name).name == name and name not in ('','.','..')
        return DATA/name

    try:
        script_before = pin(__file__)
        for path,h in ((RECEIPT,RECEIPT_SHA),(INVENTORY,INVENTORY_SHA),(INPUTS,INPUTS_SHA),
            (CAPTURE,CAPTURE_SHA),(PE_HELPER,PE_HELPER_SHA),(COFF_HELPER,COFF_HELPER_SHA),
            (FORMAT_HELPER,FORMAT_HELPER_SHA),(IMAGE,IMAGE_SHA)): pin(path,h)
        sampled,inventory,objects = map(read,(RECEIPT,INVENTORY,INPUTS))
        assert sampled['terminal'] and sampled['hashes_unchanged'] and sampled['identity_preflight_complete']
        assert sampled['status'] == 'terminal_unscored_native_location_diagnostic'
        assert sampled['mode'] == 'sample' and sampled['diagnostic_only'] and not sampled['scored'] and not sampled['acceptance']
        assert not sampled['profile_enabled'] and not sampled['fresh_body_rerun']
        assert sampled['controller_sha256'] == CAPTURE_SHA and sampled['child_sha256'] == CHILD_SHA
        assert sampled['source_inventory_sha256'] == INVENTORY_SHA
        assert sampled['git_head_at_capture'] == sampled['accepted_checkpoint_head'] == '87f5293782d21592df3bcc4ea924e659c906b6da'
        assert sampled['full97_current_binary_proof_used'] is False
        assert sampled['focused_receipt_sha256'] == '92803ffcd0287c7f17bf8431c399519891289ee143304bed7b6b4c6e1c532808'
        assert sampled['validation_sha256'] == 'd40024a60374dc2ccfa70a411f5f26f0a4177d4f870f8eb2e70ff0216bf6cba2'
        assert sampled['recorded_source_count'] == len(sampled['source_sha256']) == 119
        assert sampled['source_sha256'] == inventory['source_sha256'] and inventory['source_count'] == 119
        assert sampled['outer_loops'] == 41 and sampled['original_dump_operations'] == 2460 and sampled['protocol'] == 5
        assert sampled['hashes_before'] == sampled['hashes_after']
        for path,h in sampled['hashes_after'].items(): pin(path,h)
        for path,h in sampled['source_sha256'].items(): pin(ROOT/path,h)
        for path,h in sampled['tracked_dirty_sha256_before'].items():
            assert h is not None; pin(ROOT/path,h)
        assert sampled['tracked_dirty_sha256_before'] == sampled['tracked_dirty_sha256_after']
        release = sampled['binaries_sha256']; baseline = sampled['baseline_sha256']
        assert len(release) == 178 and release_map() == release == sampled['release_after']
        assert len(baseline) == 177 and tree(BASELINE) == baseline
        for path,h in release.items(): pin(ROOT/path,h)
        for path,h in baseline.items(): pin(BASELINE/path,h)
        assert sampled['candidate_binary_sha256']['dll'] == IMAGE_SHA
        assert sampled['candidate_binary_sha256']['exe'] == 'c6358221bd7836f877c566557269b4bf9a011a9a2ca33060eeb2e00b4e6de5b4'
        assert sampled['native_object_inputs_sha256'] == INPUTS_SHA and sampled['native_object_inputs'] == objects
        assert objects['object_count'] == len(objects['objects']) == 149
        assert objects['source_inventory_sha256'] == INVENTORY_SHA and objects['candidate_binary_sha256'] == sampled['candidate_binary_sha256']
        object_hashes = {p:r['object_sha256'] for p,r in objects['objects'].items()}
        assert sampled['object_sha256'] == sampled['object_sha256_after'] == object_hashes == current_objects()
        for path,row in objects['objects'].items():
            pin(ROOT/path,row['object_sha256']); pin(ROOT/row['source'],row['source_sha256'])
        for path,h in objects['build_metadata_sha256'].items(): pin(ROOT/path,h)
        assert sampled['body_reference']['sha256'] == '462c2bc896f601ac94102517ecd8a3646c6810d7320963aa8f152b1b05d2d074'
        assert sampled['body_reference']['historical_parent_body_sha256'] == 'c97989d6589be1eb721e608978cc69ae01227b190630ffe4f4e84564fcd1fc0e'
        assert sampled['body_reference']['historical_cpython_body_sha256'] == 'fab10dd8e053a9ca82448ba8eccc9e1cb339633602b6dcdb408d844aab882576'
        assert len(sampled['raw']) == 1
        stream = sampled['raw'][0]
        assert stream['passed'] and stream['exit_code'] == 0 and not stream['timeout'] and stream['owned_sampler_cleanup_completed']
        watch = stream['external_process_watch']
        assert stream['measurement_valid'] and watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
        pin(log_path(watch['log']),watch['sha256'])
        for name in ('stdout','stderr','child_stdout','child_stderr','samples'):
            pin(log_path(stream[name+'_log']),stream[name+'_sha256'])
        assert log_path(stream['stderr_log']).read_bytes() == log_path(stream['child_stderr_log']).read_bytes() == b''
        rows = [json.loads(line) for line in log_path(stream['samples_log']).read_text(encoding='utf-8').splitlines() if line.strip()]
        terminals = [r for r in rows if r['event'] == 'terminal']
        assert len(terminals) == 1 and rows[-1] == terminals[0] == sampled['sampler_terminal']
        terminal = terminals[0]
        assert terminal['child_exit_code'] == 0 and not terminal['timeout'] and terminal['start_seen'] and terminal['end_seen']
        samples = [r for r in rows if r['event'] == 'sample']
        assert len(samples) == sampled['sample_count'] == terminal['sample_count'] == 124
        assert all(1 <= len(s['pcs']) <= 32 and all(isinstance(pc,int) and pc > 0 for pc in s['pcs']) for s in samples)
        modules = {(m['base'],m['size'],os.path.normcase(str(Path(m['path']).resolve()))):m
            for row in rows if row['event'] == 'modules' for m in row['items']}
        assert modules and list(modules.values()) == sampled['sampled_modules']
        runtime_modules = [m for m in modules.values() if Path(m['path']).resolve() == IMAGE.resolve()]
        assert len(runtime_modules) == 1
        runtime_module = runtime_modules[0]
        assert runtime_module['base'] == 140719687204864 and runtime_module['size'] == 19976192
        sorted_modules = sorted(modules.values(),key=lambda m:m['base'])
        assert all(m['base'] > 0 and m['size'] > 0 for m in sorted_modules)
        assert all(a['base']+a['size'] <= b['base'] for a,b in zip(sorted_modules,sorted_modules[1:]))
        pe_helpers = load('postfix_pe_reader',PE_HELPER)
        coff_helpers = load('postfix_coff_reader',COFF_HELPER)
        formats = load('postfix_coff_format',FORMAT_HELPER)
        # Only the OS symbol formatter is loaded by the pinned helper. The target
        # DLL and object files are read as bytes; no target import or LoadLibrary.
        decode = pe_helpers.demangler()
        sample_pins = {os.path.normcase(str(Path(p).resolve())):h for p,h in sampled['hashes_before'].items()}
        images,image_errors = {},{}
        image = pe_helpers.PE(IMAGE)
        images[os.path.normcase(str(IMAGE.resolve()))] = image
        locations,pc_locations,targets = {},{},{}

        def locate(pc):
            if pc in pc_locations: return pc_locations[pc]
            matches = [m for m in sorted_modules if m['base'] <= pc < m['base']+m['size']]
            assert len(matches) <= 1
            if not matches:
                key = 'unknown-module:'+hex(pc)
                locations.setdefault(key,dict(location=key,module=None,confidence='unknown_module',names=[]))
                pc_locations[pc] = key; return key
            module = matches[0]; path = Path(module['path']).resolve(); normal = os.path.normcase(str(path)); rva = pc-module['base']
            expected = sample_pins.get(normal)
            if expected is None:
                key = str(path)+':unproved-image:'+hex(rva)
                locations.setdefault(key,dict(location=key,module=module,rva=hex(rva),confidence='image_not_capture_pinned',names=[]))
                pc_locations[pc] = key; return key
            if normal not in images:
                pin(path,expected)
                try: images[normal] = pe_helpers.PE(path)
                except Exception as error:
                    images[normal] = None; image_errors[str(path)] = type(error).__name__+': '+str(error)
            pe = images[normal]
            if pe is None:
                key = str(path)+':unparsed-image:'+hex(rva)
                locations.setdefault(key,dict(location=key,module=module,rva=hex(rva),confidence='pinned_image_reader_failed',names=[]))
                pc_locations[pc] = key; return key
            function = pe.containing(rva)
            if function is None:
                key = str(path)+':no-pdata:'+hex(rva)
                names = [decode(n) for n in pe.exports.get(rva,[])]
                locations.setdefault(key,dict(location=key,module=module,rva=hex(rva),
                    confidence='exact_export_without_pdata' if names else 'unresolved_without_containing_pdata',names=names))
                pc_locations[pc] = key; return key
            begin,end,unwind = function; key = str(path)+':pdata:'+hex(begin)
            if key not in locations:
                chain = pe.chain(function)
                anchors = [dict(begin=hex(f[0]),end=hex(f[1]),entries=[dict(rva=hex(entry),decorated=name,name=decode(name))
                    for entry,name in pe.export_names_in(f)]) for f in chain if pe.export_names_in(f)]
                locations[key] = dict(location=key,module=module,containing_pdata=dict(begin=hex(begin),end=hex(end),unwind=hex(unwind)),
                    confidence='export_in_containing_or_explicit_chained_pdata' if anchors else 'private_unresolved_before_coff',
                    export_anchors=anchors,names=sorted({entry['name'] for anchor in anchors for entry in anchor['entries']}))
            if path == IMAGE.resolve():
                targets.setdefault(begin,dict(begin=begin,end=end,unwind=unwind,location=key,matches=[]))
            pc_locations[pc] = key; return key

        exclusive,inclusive = Counter(),Counter()
        raw_locations = []
        for index,sample in enumerate(samples):
            leaf = locate(sample['pcs'][0]); exclusive[leaf] += 1
            stack = [locate(pc if depth == 0 else max(0,pc-1)) for depth,pc in enumerate(sample['pcs'])]
            inclusive.update(set(stack))
            raw_locations.append(dict(sample_index=index,leaf_location=leaf,stack_locations=stack,
                raw_pcs=sample['pcs'],caller_pc_minus_one=True))
        assert sum(exclusive.values()) == len(samples)
        assert len(targets) <= args.max_ranges, 'Increase bound; never truncate current observed ranges'
        raw_code = {}
        for begin,target in targets.items():
            end = target['end']; length = end-begin; offset = image.offset(begin)
            assert image.offset(end-1) == offset+length-1 and offset+length <= len(image.data)
            target['code_range_bytes'] = length
            target['match_status'] = 'skipped_below_existing_32_byte_minimum' if length < 32 else 'pending_supported_object_matching'
            raw_code[begin] = image.data[offset:offset+length]
        record['hashes_before'] = dict(pins)
        tested_functions,parsed_objects,unsupported_objects = 0,[],[]
        for relative,row in objects['objects'].items():
            path = (ROOT/relative).resolve()
            descriptor = formats.inspect_object(path); descriptor['object_sha256'] = row['object_sha256']
            if not descriptor['supported']:
                descriptor.update(parse_status='unsupported_no_native_functions_accepted',accepted_function_count=0)
                unsupported_objects.append(descriptor); continue
            try:
                # A complete object must parse before any functions are accepted.
                # Reader failure at a later symbol cannot leave partial matches.
                function_rows = list(coff_helpers.functions(path))
            except (ValueError,AssertionError,IndexError,struct.error) as error:
                descriptor.update(supported=False,parse_status='reader_failed_no_native_functions_accepted',
                    reason=type(error).__name__+': '+str(error),accepted_function_count=0)
                unsupported_objects.append(descriptor); continue
            descriptor.update(parse_status='parsed_native_coff',accepted_function_count=len(function_rows))
            parsed_objects.append(descriptor)
            for aliases,object_code,relocations,section,value in function_rows:
                tested_functions += 1
                for begin,target in targets.items():
                    length = target['code_range_bytes']
                    if length < 32 or length > len(object_code): continue
                    # The cheap prefix only rejects; accepted evidence always
                    # compares the complete containing .pdata range/prefix.
                    prefix = min(16,length)
                    left,right = bytearray(raw_code[begin][:prefix]),bytearray(object_code[:prefix])
                    for position,width in relocations:
                        assert position >= 0
                        if position < prefix:
                            amount = min(width,prefix-position); left[position:position+amount] = right[position:position+amount] = b'\0'*amount
                    if left != right: continue
                    left,right = bytearray(raw_code[begin]),bytearray(object_code[:length]); masked = set()
                    for position,width in relocations:
                        if position < length:
                            amount = min(width,length-position); left[position:position+amount] = right[position:position+amount] = b'\0'*amount
                            masked.update(range(position,position+amount))
                    if left == right:
                        target['matches'].append(dict(object=str(path),object_sha256=row['object_sha256'],section=section,
                            symbol_section_offset=value,code_bytes_matched=length,relocation_masked_bytes=len(masked),
                            aliases=[decode(name) for name in aliases],decorated_aliases=aliases))
        assert len(parsed_objects)+len(unsupported_objects) == 149
        assert all(r['accepted_function_count'] == 0 for r in unsupported_objects)
        parsed_paths = {r['path'] for r in parsed_objects}
        assert all(match['object'] in parsed_paths for target in targets.values() for match in target['matches'])
        for target in targets.values():
            if target['code_range_bytes'] >= 32:
                target['match_status'] = 'matched_compiled_prefix_with_all_aliases' if target['matches'] else 'unmatched_after_supported_native_coff_only'
        ranges = [dict(target,begin_hex=hex(begin),end_hex=hex(target['end']),
            exclusive_leaf_samples=exclusive[target['location']],exclusive_fraction_all=exclusive[target['location']]/len(samples),
            inclusive_stack_samples=inclusive[target['location']],inclusive_fraction_all=inclusive[target['location']]/len(samples))
            for begin,target in sorted(targets.items(),key=lambda pair:(exclusive[pair[1]['location']],inclusive[pair[1]['location']]),reverse=True)]
        location_rows = [dict(row,exclusive_leaf_samples=exclusive[key],exclusive_fraction_all=exclusive[key]/len(samples),
            inclusive_stack_samples=inclusive[key],inclusive_fraction_all=inclusive[key]/len(samples)) for key,row in locations.items()]
        record.update(status='terminal_file_only_coff_attribution_with_unsupported_objects',analysis_accepted=True,
            image=str(IMAGE),image_sha256=IMAGE_SHA,capture_module=runtime_module,sampled_modules=list(modules.values()),
            sample_count=124,capture_pause_statistics=sampled['pause_statistics'],capture_process_watch=watch,all_exclusive_samples_accounted=True,exclusive_leaf_total=sum(exclusive.values()),
            source_inventory_sha256=INVENTORY_SHA,recorded_source_count=119,release_count=178,baseline_count=177,
            object_count=149,object_roots=objects['object_roots'],object_sha256=object_hashes,
            object_source_mappings=objects['objects'],object_inputs_sha256=INPUTS_SHA,
            parsed_object_count=len(parsed_objects),unsupported_object_count=len(unsupported_objects),
            all_objects_reported=True,complete_native_coff_parse_coverage=not unsupported_objects,
            parsed_objects=parsed_objects,unsupported_objects=unsupported_objects,image_reader_errors=image_errors,
            tested_object_functions=tested_functions,current_observed_runtime_ranges=len(targets),all_current_ranges_included=True,
            ranges=ranges,all_locations=location_rows,raw_sample_locations=raw_locations,
            exclusive_leaf_locations=sorted(location_rows,key=lambda r:r['exclusive_leaf_samples'],reverse=True),
            inclusive_stack_locations=sorted(location_rows,key=lambda r:r['inclusive_stack_samples'],reverse=True),
            helpers_sha256=dict(pe=PE_HELPER_SHA,coff=COFF_HELPER_SHA,format_admission=FORMAT_HELPER_SHA),
            script_sha256=script_before,return_pc_minus_one=True,
            limits=[*sampled['source_identity_limits'],
                'All124 exclusive samples are partitioned; inclusive per-location stack counts overlap and are not additive self cost',
                'Leaf locations can include inline work; raw native suspensions perturb the sample distribution',
                'COFF naming requires complete containing-range prefix equality after masking only explicit COFF relocation fields',
                'The unchanged minimum is32 bytes; small ranges stay unnamed by COFF',
                'ICF/prefix-equivalent aliases remain ambiguous; relocation target identity is not proved',
                'All149 objects are reported; opaque /GL/other formats and reader failures accept zero native functions',
                'No previous capture RVAs, selected ranges or counts are reused',
                'Unpinned images and missing/unmatched code stay unresolved; no nearest-export labels',
                'Target binaries are never loaded; only the existing OS name formatter is used',
                'The body-end includes three post-body dump/load audits; no sample filtering, elapsed score or predicted gain'])
        record['hashes_before'] = dict(pins)
    except BaseException as error:
        record.update(status='terminal_failed_file_only_attribution',analysis_accepted=False,error=type(error).__name__+': '+str(error))
    finally:
        try:
            after = {p:sha(p) if Path(p).is_file() else None for p in pins}
            record.update(hashes_before=dict(pins),hashes_after=after,hashes_unchanged=after == pins,
                release_tree_unchanged=bool(release) and release_map() == release,
                baseline_tree_unchanged=bool(baseline) and tree(BASELINE) == baseline,
                object_set_unchanged=bool(objects) and current_objects() == {p:r['object_sha256'] for p,r in objects['objects'].items()})
            if not all(record[k] for k in ('hashes_unchanged','release_tree_unchanged','baseline_tree_unchanged','object_set_unchanged')):
                record.update(status='terminal_invalid_file_analysis_identity',analysis_accepted=False)
                # Global identity failure cannot publish accepted function names.
                record.pop('ranges',None); record.pop('all_locations',None)
                record.pop('exclusive_leaf_locations',None); record.pop('inclusive_stack_locations',None)
        except BaseException as error:
            record.update(status='terminal_invalid_final_file_identity',analysis_accepted=False,hashes_unchanged=False,final_identity_error=repr(error))
            for key in ('ranges','all_locations','exclusive_leaf_locations','inclusive_stack_locations'): record.pop(key,None)
        record['terminal'] = True; record['completed_utc'] = datetime.now(timezone.utc).isoformat()
        with args.output.open('xb') as stream: stream.write((json.dumps(record,indent=2)+'\n').encode('utf-8'))
    print('File-only native attribution:',record['status'],'accepted',record['analysis_accepted'],
        'runtime ranges',record.get('current_observed_runtime_ranges'),'samples',record.get('sample_count'),flush=True)
    return 0 if record['analysis_accepted'] else 1


if __name__ == '__main__': raise SystemExit(main())
