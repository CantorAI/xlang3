"""Reuse C5 correctness phases, add unchanged threading check, pin actual R6."""
import argparse, hashlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--inventory-sha256',required=True)
args = parser.parse_args()
source = ROOT / 'scratch/performance/check-class-constructor-plan-c5-focused-20261008.py'
assert hashlib.sha256(source.read_bytes()).hexdigest() == 'ce3b3ad1e7bccab86b087f5a569610a8c1946a5175e73d37ddc48b6d5b89ad3e'
target = source.with_name('check-published-frame-snapshot-r6-focused-20261008.py')
assert not target.exists()
raw = source.read_text()
changes = {
 'class-constructor-plan-c5-applied-source-20261008.json':'published-frame-snapshot-r6-applied-source-20261008.json',
 '69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937':args.inventory_sha256,
 'class-constructor-plan-c5-focused-20261008':'published-frame-snapshot-r6-focused-20261008',
 "('annotation2', 'class_method_annotation_capture'), ('cpp', None)":"('annotation2', 'class_method_annotation_capture'), ('threading', 'threading_runtime_edges'), ('cpp', None)",
 'Eleven targeted correctness phases':'Twelve targeted correctness phases'}
for before,after in changes.items():
    assert raw.count(before) == 1, before
    raw = raw.replace(before,after)
target.write_text(raw,encoding='utf-8')
print(hashlib.sha256(target.read_bytes()).hexdigest())
