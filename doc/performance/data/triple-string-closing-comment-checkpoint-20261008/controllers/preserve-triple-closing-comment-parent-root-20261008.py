"""Preserve the validated current Release and parser targets before any edits."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

assert sys.version_info[:3] == (3,14,7) and not sys.flags.optimize
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
ROOT = Path('D:/CantorAI/xlang3')
RELEASE = ROOT/'build-repro/main-verify-20261006/Release'
BASELINE = ROOT/'build-repro/Release'
CONTROL = ROOT/'build-repro/controls/triple-string-closing-comment-parent-20261008'
DATA = ROOT/'doc/performance/data'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree = lambda p: {q.relative_to(p).as_posix():sha(q) for q in sorted(p.rglob('*')) if q.is_file()}
def document(p,h):
    assert sha(p)==h,str(p)
    return json.loads(p.read_bytes())
assert not CONTROL.exists()
trial = document(DATA/'native-str-utf8-validation-resume-r2-20261008.json','d40024a60374dc2ccfa70a411f5f26f0a4177d4f870f8eb2e70ff0216bf6cba2')
sample = document(DATA/'pickle-native-utf8-postfix-native-sampling-r2-20261008.json','4c627d679d44d9d89b8dd8799f52a6ff1dfbba3356d7dde4f81c8f6b0f99a4fa')
inputs = document(ROOT/'scratch/performance/pickle-native-utf8-postfix-native-sampling-inputs-proposed-20261008.json','a8be77e20bab1a752a870872e194ecabe3354626398916d6e155510002b282d5')
assert trial['terminal'] and trial['full_validated'] and trial['hashes_unchanged']
assert sample['terminal'] and sample['hashes_unchanged'] and sample['raw'][0]['passed']
sources = dict(trial['source_sha256']); assert len(sources)==119
for p in ['src/parser/lexer.cpp','src/parser/parser.cpp','src/internal/xlang3/parser.h','src/internal/xlang3/ast.h','tests/cpp/parser_tests.cpp']:
    value=sha(ROOT/p)
    assert p not in sources or sources[p]==value
    sources[p]=value
release = tree(RELEASE); baseline = tree(BASELINE)
assert len(release)==178 and len(baseline)==177
assert {RELEASE.relative_to(ROOT).as_posix()+'/'+p:h for p,h in release.items()}==sample['binaries_sha256']==trial['binaries_sha256']
assert baseline==trial['baseline_sha256']==sample['baseline_sha256']
objects = {p:r['object_sha256'] for p,r in inputs['objects'].items()}
translation_units = {r['source']:r['source_sha256'] for r in inputs['objects'].values()}
assert len(objects)==149 and objects==sample['object_sha256']
metadata = inputs['build_metadata_sha256']
head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
assert head=='87f5293782d21592df3bcc4ea924e659c906b6da'
assert not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=ROOT)
dirty = {p:sha(ROOT/p) for p in subprocess.check_output(['git','-c','core.safecrlf=false','diff','HEAD','--name-only','-z'],cwd=ROOT).decode().split('\0') if p}
for p,h in {**sources,**objects,**translation_units,**metadata}.items(): assert sha(ROOT/p)==h,p
def capture(source,destination,expected):
    target=(CONTROL/destination).resolve()
    assert target.is_relative_to(CONTROL.resolve()) and not target.exists()
    target.parent.mkdir(parents=True,exist_ok=True)
    assert sha(source)==expected
    shutil.copyfile(source,target)
    assert sha(source)==sha(target)==expected
for p,h in release.items(): capture(RELEASE/p,'Release/'+p,h)
for group,values in [('source-snapshot',sources),('native-objects',objects),('translation-units',translation_units),('build-metadata',metadata)]:
    for p,h in values.items(): capture(ROOT/p,group+'/'+p,h)
assert tree(RELEASE)==release and tree(BASELINE)==baseline
for p,h in {**sources,**objects,**translation_units,**metadata,**dirty}.items(): assert sha(ROOT/p)==h,p
record=dict(status='preserved_validated_triple_closing_comment_parent',terminal=True,full_validated=True,
    head=head,runtime_path=str(RELEASE/'xlang3.exe'),file_count=len(release),source_count=len(sources),object_count=len(objects),
    files_sha256=release,source_snapshot_sha256=sources,object_snapshot_sha256=objects,
    translation_unit_snapshot_sha256=translation_units,build_metadata_sha256=metadata,
    fixed_baseline_sha256=baseline,tracked_dirty_sha256=dirty,controller_sha256=sha(Path(__file__)),
    validation_sha256='d40024a60374dc2ccfa70a411f5f26f0a4177d4f870f8eb2e70ff0216bf6cba2',
    source_identity_limit='Original recorded119 plus five parser targets and separately pinned current native translation units; not all transitive headers or a clean-checkout claim',engine_changes=False)
(CONTROL/'preserved-release-provenance.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
print('Preserved Release',len(release),'sources',len(sources),'native objects',len(objects),'fixed baseline',len(baseline))
