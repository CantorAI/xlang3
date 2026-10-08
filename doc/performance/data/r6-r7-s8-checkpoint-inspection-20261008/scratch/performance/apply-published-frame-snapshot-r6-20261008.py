"""Apply reviewed snapshot engine/public-test candidates to preserved C5."""
import argparse, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p): return json.loads(Path(p).read_bytes())
parser = argparse.ArgumentParser()
parser.add_argument('--cpp-proof',type=Path,required=True)
parser.add_argument('--cpp-proof-sha256',required=True)
args = parser.parse_args()
assert sys.version_info[:3] == (3,14,7)
assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip() == 'e6913f8fa33eb0ba0d452f451808ec9df3d5ff4d'
inventory = DATA / 'class-constructor-plan-c5-applied-source-20261008.json'
assert sha(inventory) == '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937'
sources = read(inventory)['source_sha256']
assert len(sources) == 106 and all(sha(ROOT / p) == h for p,h in sources.items())
control = ROOT / 'build-repro/controls/class-constructor-c5-before-snapshot-r6-20261008'
manifest = control / 'preserved-release-provenance.json'
assert sha(manifest) == '85910ee375f16c677153fc128fb17303868a849c124f5909d5ee03eeed764bdc'
preserved = read(manifest)
release = ROOT / 'build-repro/main-verify-20261006/Release'
assert preserved['source_snapshot_sha256'] == sources
for p,h in preserved['files_sha256'].items(): assert sha(control / p) == sha(release / p) == h
for p,h in sources.items(): assert sha(control / 'source-snapshot' / p) == h
engine_proof = ROOT / 'scratch/performance/published-frame-same-owner-refresh-proposed-20261008-provenance.json'
assert sha(engine_proof) == 'ab41aa60a3e33963f8b1e1525631cc3c4ded1a83a4f601faeb0819dfa44972e6'
assert sha(args.cpp_proof) == args.cpp_proof_sha256
proposals = [read(engine_proof),read(args.cpp_proof)]
targets = {}
for proposal in proposals:
    for p,h in proposal['raw_before_sha256'].items():
        assert (sha(ROOT / p) == h) if h is not None else not (ROOT / p).exists()
    for p,h in proposal['candidate_source_sha256'].items():
        assert p not in targets and (ROOT / p).resolve().is_relative_to(ROOT)
        if p not in sources: assert not (ROOT / p).exists()
        candidate = Path(proposal['candidate_root']) / p
        assert sha(candidate) == h
        targets[p] = (candidate,h)
assert {'src/runtime/runtime.cpp','tests/cpp/interpreter_tests.cpp'} <= set(targets)
assert len(targets) == 3 and all(p in sources or (p.startswith('tests/cpp/') and p.endswith('.h')) for p in targets)
rows = json.loads(subprocess.check_output(['powershell','-NoProfile','-Command','Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig'))
assert not any(r['ProcessId'] != os.getpid() and (r['Name'].lower().startswith(('python','xlang3')) or r['Name'].lower() in {'cl.exe','link.exe','cmake.exe','ninja.exe','ctest.exe','msbuild.exe'}) for r in rows)
output = DATA / 'published-frame-snapshot-r6-applied-source-20261008.json'
assert not output.exists()
for p,(candidate,h) in targets.items():
    shutil.copy2(candidate,ROOT / p)
    assert sha(ROOT / p) == h
sources = {p:sha(ROOT / p) for p in set(sources)|set(targets)}
output.write_text(json.dumps(dict(status='applied_unbuilt_unvalidated_snapshot_r6',parent_inventory=str(inventory),
 parent_inventory_sha256=sha(inventory),preserved_control=str(manifest),preserved_control_sha256=sha(manifest),
 engine_proof_sha256=sha(engine_proof),cpp_proof_sha256=sha(args.cpp_proof),
 source_count=len(sources),source_sha256=sources,applied_targets={p:h for p,(_,h) in targets.items()},
 runtime_execution=False,performance_validated=False),indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(inventory_sha256=sha(output),source_count=len(sources))))
