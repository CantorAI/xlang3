$ErrorActionPreference='Stop'
$root='D:/CantorAI/xlang3'
$utf8=[System.Text.UTF8Encoding]::new($false)
$old='scratch/performance/run-sorted-exact-int-s8-original-official-pyflate-20261008.py'
$new='scratch/performance/run-sorted-exact-int-s8-original-official-pyflate-idle-resume-20261008.py'
function Sha([string]$p){(Get-FileHash -LiteralPath (Join-Path $root $p) -Algorithm SHA256).Hash.ToLowerInvariant()}
if((Sha $old)-ne'3dbfc68c563c30b615d7a4770b891f984b9f576fa51b49985ae78ba8b060ef29'){throw 'Preserve oldmanager'}
if(Test-Path -LiteralPath (Join-Path $root $new)){throw 'Do notoverwritefreshmanager'}
$script:text=[System.IO.File]::ReadAllText((Join-Path $root $old)).Replace("`r`n","`n")
function Change([string]$a,[string]$b){if(($script:text.Split(@($a),[System.StringSplitOptions]::None).Count-1)-ne1){throw ('Boundary notunique: '+$a)};$script:text=$script:text.Replace($a,$b)}
Change 'One original official pyflate attempt after matching S8 correctness/default gate.' 'One fresh original official pyflate attempt with exact parked-worker activity checks.'
Change "WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'" @'
WATCH = ROOT / 'scratch/performance/verified-idle-msbuild-policy-proposed-20261008.py'
WATCH_SHA = '4063d997ff08551eba3a909e3b897758518854645a6521efee32bda0307f5be4'
TEST_SOURCE = ROOT / 'scratch/performance/test-verified-idle-msbuild-policy-proposed-20261008.py'
TEST_SOURCE_SHA = '7d6891b513e689c9346f14fa5c6f4f0b533fdc750412a151822f227db0e9295e'
WORKER_PROOF = DATA / 'verified-idle-msbuild-worker-20261008.json'
WORKER_PROOF_SHA = '5f9522080e4309aba05149fdbddf2daeaaef0c8fbdefc18d7cf713d4750ec210'
CONSOLE_PROOF = DATA / 'verified-idle-msbuild-console-20261008.json'
CONSOLE_PROOF_SHA = '63a7bc6f8f91b82c7a5c8136aff8f403179ea7bd6e67c8956a148e265c5ddeb1'
FAILED = DATA / 'sorted-exact-int-s8-original-official-pyflate-20261008.json'
FAILED_SHA = '545c506ae452bacf2c993fb35253c0d59c430f146e4147ab2517220c48515881'
FAILED_CONTROLLER = ROOT / 'scratch/performance/run-sorted-exact-int-s8-original-official-pyflate-20261008.py'
'@
Change "PREFIX = 'sorted-exact-int-s8-original-official-pyflate-20261008'" "PREFIX = 'sorted-exact-int-s8-original-official-pyflate-idle-resume-20261008'"
Change "    parser.add_argument('--source-inventory-sha256', required=True)" @'
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--policy-test-receipt', type=Path, required=True)
    parser.add_argument('--policy-test-receipt-sha256', required=True)
'@
Change "    pins = {}`n" "    pins = {}`n    watcher = None`n    activity_policy = None`n"
$oldIdle=@'
    def idle(label):
        rows = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        tools = {'cl.exe','link.exe','ninja.exe','cmake.exe','ctest.exe','msbuild.exe','nmake.exe','clang-cl.exe','lld-link.exe'}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
                (r['Name'].lower().startswith(('python', 'xlang3')) or r['Name'].lower() in tools)]
        record['idle_guards'].append(dict(phase=label, busy=busy)); save(); assert not busy, busy
'@
$newIdle=@'
    def idle(label):
        snapshot = watcher.scan(activity_policy)
        result = watcher.evaluate(snapshot, activity_policy, os.getpid())
        record['idle_guards'].append(dict(phase=label, snapshot=snapshot, policy=result)); save()
        assert result['valid'], result
'@
Change $oldIdle $newIdle
Change "        pin(Path(__file__)); pin(FULL_CONTROLLER, '997d0e694f5c04aa2459bfbaca36065054c53d1661f810e37c5f7dd7b50813f3')" @'
        pin(Path(__file__)); pin(FULL_CONTROLLER, '997d0e694f5c04aa2459bfbaca36065054c53d1661f810e37c5f7dd7b50813f3')
        pin(FAILED, FAILED_SHA); pin(FAILED_CONTROLLER, '3dbfc68c563c30b615d7a4770b891f984b9f576fa51b49985ae78ba8b060ef29')
        failed = json.loads(FAILED.read_bytes())
        assert failed['terminal'] and failed['hashes_unchanged'] and failed['raw'] == []
        assert failed['status'] == 'terminal_failed_official_pyflate_preflight_or_execution'
        assert failed['idle_guards'][-1]['busy'] == [dict(Name='MSBuild.exe', ProcessId=30756)]
        pin(WATCH, WATCH_SHA); pin(TEST_SOURCE, TEST_SOURCE_SHA)
        tests_path = args.policy_test_receipt.resolve(strict=True); pin(tests_path, args.policy_test_receipt_sha256)
        tests = json.loads(tests_path.read_bytes())
        assert tests['terminal'] and tests['status'] == 'terminal_policy_tests_passed'
        assert tests['cpython_version'].startswith('3.14.7 ') and tests['helper_sha256'] == WATCH_SHA
        assert tests['test_source_sha256'] == TEST_SOURCE_SHA and tests['test_count'] == 26
        assert len(tests['cases']) == 26 and all(case['passed'] for case in tests['cases'])
        pin(WORKER_PROOF, WORKER_PROOF_SHA); pin(CONSOLE_PROOF, CONSOLE_PROOF_SHA)
        spec = importlib.util.spec_from_file_location('verified_worker_activity', WATCH)
        watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
        activity_policy = watcher.policy_from_proofs(json.loads(WORKER_PROOF.read_bytes()), json.loads(CONSOLE_PROOF.read_bytes()))
        record['prior_failed_preflight'] = dict(record=FAILED.name, sha256=FAILED_SHA, timed_child_started=False)
        record['required_activity_policy'] = dict(source_sha256=WATCH_SHA, test_source_sha256=TEST_SOURCE_SHA,
            test_receipt=str(tests_path), test_receipt_sha256=sha(tests_path), worker_proof_sha256=WORKER_PROOF_SHA,
            console_proof_sha256=CONSOLE_PROOF_SHA, pinned_worker_pid=30756, pinned_console_pid=9220,
            scope='Only exact immutable worker+console identities and unchanged integer kernel/user CPU counters; any activity, unknown descendants, other builds/runtime conflicts or scanner failure invalidates')
'@
Change "        assert full['terminal'] and full['correctness_passed'] and full['hashes_unchanged'] and full['release_tree_unchanged']" @'
        assert full['terminal'] and full['correctness_passed'] and full['hashes_unchanged'] and full['release_tree_unchanged']
        assert failed['source_inventory_sha256'] == sha(inventory_path) and failed['source_sha256'] == inv['source_sha256']
'@
Change "        pin(WATCH, '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf')`n        spec = importlib.util.spec_from_file_location('pyflate_official_timing_watch', WATCH)`n        watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)`n" ''
Change "        finish = watcher.start_timing_process_watch(PREFIX, 'official-pyflate', row); child = None" "        finish = watcher.start_watch(DATA, PREFIX, row, activity_policy); child = None"
Change "        record.update(status='terminal_failed_official_pyflate_preflight_or_execution', error=repr(error))" @'
        record.update(status='terminal_failed_official_pyflate_preflight_or_execution', error=repr(error))
        if 'official_pyflate' in record:
            record['official_pyflate'].update(complete=False, invalidated_by_guard_or_execution=True)
'@
[System.IO.File]::WriteAllText((Join-Path $root $new),$script:text,$utf8)
[ordered]@{source=$new;sha256=Sha $new}|ConvertTo-Json
