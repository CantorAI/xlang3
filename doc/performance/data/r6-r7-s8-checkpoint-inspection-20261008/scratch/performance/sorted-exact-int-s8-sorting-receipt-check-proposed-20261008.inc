    def verify_sorting_paired_receipt():
        import random
        import statistics
        path = args.sorting_paired_receipt.resolve(strict=True)
        track(path, args.sorting_paired_receipt_sha256)
        paired = json.loads(path.read_text(encoding='utf-8'))
        producer = ROOT / 'scratch/performance/measure-sorted-exact-int-s8-paired-20261008.py'
        producer_sha = '5fc9504823e58d81555d39869b75bdb5c8a9a8ec6ad94df00bb7e23e433867c2'
        child = ROOT / 'benchmarks/diagnostics/python_callback_boundary.py'
        child_sha = 'f76136594ac8e64e5fb0d71a47eff3af286d5e12178a8acbaa7fe4c43a6ef84a'
        track(producer, producer_sha); track(child, child_sha)
        assert paired['terminal'] and paired['status'] == 'terminal_unscored_paired_callback_boundary'
        assert paired['hashes_unchanged'] and paired['hashes_before'] == paired['hashes_after']
        assert not paired['scored'] and not paired['full_validated']
        assert paired['controller_sha256'] == producer_sha and paired['child_sha256'] == child_sha
        assert paired['source_inventory_sha256'] == digest(inventory_path) and paired['source_sha256'] == sources
        assert paired['focused_receipt_sha256'] == digest(focused_path)
        assert paired['binaries_sha256'] == record['binaries_sha256']
        assert (paired['pair_count'], paired['total_child_invocations'], paired['total_timed_case_invocations']) == (7, 14, 210)
        assert (paired['warmup_case_invocations_per_child'], paired['timed_samples_per_case_per_child']) == (5, 3)
        assert paired['values_trimmed'] == paired['outliers_removed'] == 0
        assert paired['decision_rule'] == dict(selected_case='sort_plain', minimum_median_ratio=1.10,
            minimum_lower_ci=1.0, bootstrap_resamples=50000, bootstrap_seed=20261008, strict_greater_than=True)
        assert paired['hashes_before'][str(producer)] == producer_sha
        assert paired['hashes_before'][str(child)] == child_sha
        manifest = C5_CONTROL / 'preserved-release-provenance.json'
        assert paired['hashes_before'][str(manifest)] == C5_CONTROL_MANIFEST_SHA
        for pinned_path, sha in paired['hashes_before'].items(): track(Path(pinned_path), sha)
        cases = ('loop', 'small_calls', 'branch_calls', 'sort_plain', 'sort_callback')
        assert len(paired['pair_results']) == 7 and len(paired['raw']) == 14
        ratios, all_rows = [], []
        for index, pair in enumerate(paired['pair_results']):
            order = ['control', 'candidate'] if index % 2 == 0 else ['candidate', 'control']
            assert pair['pair_index'] == index and pair['order'] == order
            assert set(pair['runs']) == {'control', 'candidate'}
            for role in order:
                row = pair['runs'][role]; all_rows.append(row)
                assert row['pair_index'] == index and row['runtime'] == role
                assert row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['measurement_valid']
                executable = (C5_CONTROL / 'xlang3.exe') if role == 'control' else CANDIDATE
                assert [Path(value).resolve() for value in row['command']] == [executable.resolve(), child.resolve()]
                for stream in ('stdout', 'stderr'): track(DATA / row[stream + '_log'], row[stream + '_sha256'])
                assert (DATA / row['stderr_log']).read_bytes() == b''
                parsed, indices = {case: [] for case in cases}, {case: [] for case in cases}
                lines = (DATA / row['stdout_log']).read_text(encoding='utf-8-sig').splitlines()
                assert len(lines) == 15
                for line in lines:
                    marker, case, sample, count, value = line.split()
                    assert marker == 'callback_boundary' and case in parsed and int(count) == 50000
                    elapsed = float(value)
                    assert math.isfinite(elapsed) and elapsed > 0
                    parsed[case].append(elapsed); indices[case].append(int(sample))
                assert all(values == [0, 1, 2] for values in indices.values()) and row['times'] == parsed
                watch = row['external_process_watch']
                assert watch['measurement_valid']
                track(DATA / watch['log'], watch['sha256'])
            ratio = statistics.median(pair['runs']['control']['times']['sort_plain']) / statistics.median(pair['runs']['candidate']['times']['sort_plain'])
            assert math.isfinite(ratio) and ratio > 0 and pair['ratios']['sort_plain'] == ratio
            ratios.append(ratio)
        assert all_rows == paired['raw'], 'Retain every original child row in predeclared order'
        rng = random.Random(20261008)
        draws = sorted(statistics.median([ratios[rng.randrange(7)] for _ in range(7)]) for _ in range(50000))
        def percentile(q):
            position = q * (len(draws) - 1); lower = math.floor(position); upper = math.ceil(position)
            return draws[lower] + (draws[upper] - draws[lower]) * (position - lower)
        median = statistics.median(ratios); interval = [percentile(.025), percentile(.975)]
        summary = paired['summary']
        assert summary['pair_ratios'] == ratios and summary['median_pair_ratio'] == median
        assert summary['bootstrap95_ci'] == interval
        assert summary['all_cases']['sort_plain'] == dict(pair_ratios=ratios, median_pair_ratio=median, bootstrap95_ci=interval)
        assert median > 1.10 and interval[0] > 1.0 and summary['useful_signal']
        assert summary['decision'] == 'signal_for_full_checkpoint_validation'
        record['required_sorting_paired_diagnostic'] = dict(record=path.name, sha256=digest(path),
            controller_sha256=producer_sha, child_sha256=child_sha, pair_count=7, total_children=14,
            selected_case='sort_plain', pair_ratios=ratios, median_pair_ratio=median, bootstrap95_ci=interval,
            minimum_median_ratio=1.10, minimum_lower_ci=1.0, values_trimmed=0, outliers_removed=0,
            scope='Predeclared seven-pair unchanged diagnostic C5/S8 sorting signal only; no official score, isolated sorting-vs-module causality or CPython win claim')
        save()
