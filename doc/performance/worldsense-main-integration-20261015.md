# WorldSense integration of main at f6bbc561

Merge main `f6bbc561673e3c166dc0dab9062cb24db26432a1` into the WorldSense
acceptance branch at `f67422a5460e6b2affdcfc067dcb8fb0f2df1195`. Preserve
the branch's registry public-key API, cryptography and descriptor fixes,
constructor ownership behavior and Windows exclusive listener binding.

The automatic merge duplicated the general descriptor fallback check. Keep
one check and preserve the proven property getter path plus the native
MemoryBIO property fallback. Both descriptor fixtures and native HTTP/SSL
contracts remain required. This does not introduce a new optimization trial.

Before integration, the fixed accepted GitHub Release baseline
`20827bcaa4846728cf152fec16a2e0e572461dc7` was preserved in a separate inactive
folder. Archive SHA256 is
`4a0a03b84cafd6c4b8cf3f10ea7a095068720e62d783f9eece08d723ef9b368d`;
executable is `8e32fd495e16dcb607c53afb0ab271689b5781d4014d33e35584c5170fa1d3d5`
and runtime DLL is `fb32498d43868dbdd4aeb8026198e13a005862c9515c741539c88a7b50efdd01`.
No application installation or local native compilation was performed.

Use the user's prior authorization to push draft source for GitHub testing.
Native correctness and performance acceptance are **pending**, not passed.
Upstream's own performance report does not replace the fixed acceptance
baseline. After the GitHub Release candidate passes all native contracts and
the complete CLI fixture suite, run the unchanged `check_regression.py` on an
idle host with no builds or competing benchmarks: all 11 cases, 21 paired
samples, five warmups and the existing 10% threshold. Preserve exact exit
status and JSON. Exit 1 or 2 does not permit acceptance; do not change the
baseline, cases, thresholds or workloads to obtain a pass.

The matching WorldSense dependency pair must be Pydantic2.14.1/Core2.50.1,
matching main's bundled native implementation and Python facade. Existing
2.13.5/2.46.5 receiving evidence remains scoped to its previous source.
