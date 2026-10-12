"""Rebase metadata guards to the committed, byte-identical VM checkpoint."""
import ast
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT/'scratch/performance'
OLD = SCRATCH/'run-python-new-vm-continuation-r4-full-pyperformance-refresh-r2-root-20261009.py'
NEW = SCRATCH/'run-python-new-vm-continuation-r4-full-pyperformance-checkpoint-r3-root-20261009.py'
PROOF = SCRATCH/'python-new-full97-checkpoint-r3-root-provenance-20261009.json'
sha = lambda data: hashlib.sha256(data).hexdigest()

def main():
    old = OLD.read_bytes()
    assert sha(old) == 'f5587d5326658062b90e1b1acfb531bf0d4cc79f9ce1a363ed1de01b180adbca'
    head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    assert head == '5997b264f8b71c38b57c98763788702f38146217'
    assert not NEW.exists() and not PROOF.exists()
    attributes = b''.join(line.encode()+b'\n' for line in (
        '/doc/performance/data/python-new-* -text',
        '/doc/performance/data/python-new-*/** -text',
        '/doc/performance/data/pyperformance-xlang3-python-new-r4-* -text',
        '/doc/performance/data/pyperformance-xlang3-python-new-r4-*/** -text',
        '/doc/performance/data/date-constructor-entry-cp-r5-r4-* -text',
        '/doc/performance/python-new-vm-continuation-r4-checkpoint-20261009.md text eol=lf',
        '/doc/performance/data/date-constructor-entry-balanced-* -text'))
    raw = (ROOT/'.gitattributes').read_bytes()
    original_attrs_sha = '751c445e9450cf1058f4d252166c0ee644d9a825428496fc53d904e49d59986a'
    assert raw.endswith(attributes) and sha(raw[:-len(attributes)]) == original_attrs_sha
    text = old.decode()
    literal = "CURRENT_HEAD = '4b1cfefc80f0bbb9e1f60c09c46d83619ded0e17'"
    assert text.count(literal) == 1
    text = text.replace(literal, "CURRENT_HEAD = '"+head+"'")
    needle = '        if expected is not None: assert value == expected, str(path)\n'
    assert text.count(needle) == 1
    replacement = (
        "        # The committed checkpoint appends only these seven raw-doc attributes.\n"
        "        # Authenticate the complete original unowned prefix, then pin the\n"
        "        # actual appended bytes for this run. No engine/source hash is rebased.\n"
        "        if path == (ROOT / '.gitattributes').resolve() and expected is not None:\n"
        "            assert expected == '"+original_attrs_sha+"'\n"
        "            metadata_suffix = "+repr(attributes)+"\n"
        "            metadata_raw = path.read_bytes()\n"
        "            assert metadata_raw.endswith(metadata_suffix)\n"
        "            assert hashlib.sha256(metadata_raw[:-len(metadata_suffix)]).hexdigest() == expected\n"
        "            expected = value\n" + needle)
    text = text.replace(needle,replacement)
    needle = "    def save():\n"
    assert text.count(needle) == 1
    rebase_record = "    record['metadata_only_checkpoint_rebase'] = " + repr({
        'committed_head': head, 'validation_head': '4b1cfefc80f0bbb9e1f60c09c46d83619ded0e17',
        'unchanged_engine_source_count':132, 'original_working_attributes_sha256':original_attrs_sha,
        'appended_doc_attributes_sha256':sha(attributes), 'actual_working_attributes_sha256':sha(raw),
        'scope':'Committed HEAD and exactly seven documentation attributes only; unchanged source/binaries, benchmark selection, workloads, timeouts and strict process watcher.'}) + "\n\n"
    text = text.replace(needle,rebase_record+needle)
    text = text.replace('Current R4 trial HEAD differs from supplied exact4b HEAD', 'Committed R4 checkpoint HEAD differs from supplied exact HEAD')
    candidate = text.encode()
    ast.parse(candidate)
    # The timed execution region is identical to the previously reviewed runner.
    assert text[text.index('    child = None'): ] == old.decode()[old.decode().index('    child = None'): ]
    app = json.loads((ROOT/'doc/performance/data/python-new-vm-continuation-r4-applied-source-20261009.json').read_bytes())
    assert len(app['source_sha256']) == 132
    for path,expected in app['source_sha256'].items(): assert sha((ROOT/path).read_bytes()) == expected,path
    NEW.write_bytes(candidate)
    proof={'status':'prepared_metadata_only_checkpoint_rebase','parent':str(OLD),'parent_sha256':sha(old),
        'controller':str(NEW),'controller_sha256':sha(candidate),'head':head,
        'engine_source_unchanged':True,'timed_execution_region_identical':True,
        'original_working_attributes_sha256':original_attrs_sha,'actual_working_attributes_sha256':sha(raw),
        'appended_doc_attributes_sha256':sha(attributes),'ast_passed':True,'executed':False}
    PROOF.write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(proof))

if __name__ == '__main__': main()
