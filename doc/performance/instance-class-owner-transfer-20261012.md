# Instance class owner transfer

The downloaded WorldSense candidate workspace `1e34d50f268f9cc71e04037fe9f9c963b199ac68`
uses XLang3 `9a11cc6f97d5c4cc222ccfd58f97418ab0cc0a33`. Its native contracts
and all 446 fixtures passed, but the unchanged complete fixed-baseline gate
failed object construction twice. The confirmation ratio was 1.107745 with
a 95% interval of [1.100354, 1.113788] against accepted workspace
`20827bcaa4846728cf152fec16a2e0e572461dc7`. The gate, baseline and thresholds
remain unchanged.

Unscored diagnostics on the actual Weican Windows GPU machine retained the
exact default constructor source (SHA256
`36316f6f4b80e554e9ffc93a1b1406b4a8ed25d3ede0c65247b77934cdafb12d`).
Baseline and candidate have identical initializer, method and main-loop IR;
only module startup differs. Their instrumented runs each allocated 100001
instances and recorded 200010 class retains. A separate child-only native
sample repeated the original 100000-iteration main body 2000 times, preserving
all 2001 expected outputs per runtime. It collected 2300 baseline and 2530
candidate instruction-pointer samples. Exact exported starts in PE unwind
ranges attribute 429/456 samples to Value copy assignment and 304/341 to
Value::instance. Unnamed private ranges remain unresolved. These samples
include inline work and perturb execution; they are neither timings nor proof
of a single regression cause.

Value::instance takes its class by value, then copies that already-owned
argument into the fresh instance and drops the temporary. The class parameter
has no later use. Transfer it through value_move_assign_fast instead: an owned
parameter moves directly, while a moved borrowed parameter still gets promoted
to ownership. The signature, native ABI, class/container layout, GC tracking,
initializer and observer guards, finalizers and ordinary fallbacks are unchanged.
No Python library implementation is replaced.

Before this experiment, prior performance reports and patches were searched
for Value::instance, obj->klass, instance/class owner transfer and reference
traffic. The accepted Value self-assignment and refcount ordering trials, the
constructor-plan trials and the rejected call-entry owner-selection and
diagnostic-counter removal trials were reviewed. This change transfers an
unused class parameter at allocation; it does not repeat the rejected call-entry
selection or remove counters.

The new C++ checks cover lvalue lifetime across repeated recycling, moved owner
lifetime and absence of an extra internal retain, moved borrowed-reference
promotion, and survival after the source class owner disappears. A plain
std::move would fail the borrowed-owner checks. Existing initializer/method,
native constructor, exception and class-mutation tests remain in place.

Status: source correction prepared for a draft GitHub build under the user's
explicit draft-commit exception. It is not yet performance validated or accepted.
Run the native C++ checks through GitHub Actions, then download the new versioned
GitHub Release for all native contracts, all 446 fixtures and the complete default
11-case/21-pair/five-warmup performance gate on an idle Weican machine. Only a
passing gate permits application installation. Raw diagnostics are preserved in
the task's release-acceptance evidence directory; no application service changed.
