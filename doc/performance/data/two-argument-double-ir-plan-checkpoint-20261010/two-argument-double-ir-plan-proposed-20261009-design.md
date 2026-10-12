Bounded two-argument Double IR plan — scratch-only controlled trial

Root observed the unchanged original raytrace worker once: receipt d77f921e.
Vector.dot has20registers/15instructions: CallLocalMethod, Pop,6LoadLocalAttr,
3Mul,2Add,Return and unreachable None ReturnConst. Its original guard is
ReturnLocal(0). Existing small-self execution requires1parameter/at most16regs.
The dump establishes compile IR; it establishes neither adaptive admission,
frame counts, cost share nor gain.

Only2engine headers change. The uncached helper recognizes generic fused IR,
without class/method/module/field-name specialization. Exact2positional synchronous
methods with no closure/cell/local state, at most32regs/24instructions, a leading
zero-argument method whose result is immediately discarded, and unique scalar
field loads/Add/Sub/Mul/Return are eligible. Nonfamily shapes are rejected before
observer checks, ownership copies or class lookup. Whole shape checks precede
lookup, all lookup/Double guards precede arithmetic, and no callable/descriptor
is speculatively executed. Unsupported inputs take the original frame once.

Freshly resolve the nested ordinary Function through the argument's class/MRO;
reuse the existing trivial analyzer to prove return argument0. Reject native
getters, custom getattribute, slots, materialized attribute dictionaries, callable
shadows, any class attribute for a field, missing/non-Double fields, async/coroutine/
generator/closure state and all trace/profile/debug-hook/monitoring observers.
No persistent plan/owner, CallSiteCache expansion, ABI, magic or codec changes.
Current module/input/nested callable owners protect reads. Helper returns a fresh
Double; caller publishes into the live destination and takes normal Next refresh,
with no later function/class/cache/instance access on that success branch.

Physical-layout source audit: lower.cpp4650–4746 publishes __static_attributes__
but deliberately leaves ordinary classes dictionary-backed; own_instance_slots
derives explicit __slots__. Vector has no slots and base object has none.
object_model.cpp2895 sizes physical slots from class.instance_slot_names.
inline_call.h1213–1247 ordinary canonical constructor appends named attrs under
kXlangVMInlineConstructorAttrFlag. This supports zero slots/native attrs, but
author has NOT inspected a live Vector layout. Reading __dict__ before admission
would itself make the prototype decline. Mixed Int64 coordinates decline.
Actual original-body eligible fraction remains unknown and must not be inferred
from output parity or this dump.

History: raytrace analysis b21da7 is old/profile-active evidence. Rejected Double
arithmetic413756, generic exact-call65a5 and polymorphic-cachec82c retain normal
frames. Preserved forwarder1c85 delegates a BytesIO writer, binder8202 changes
binding, polymorphic-call47ee caches identity. Targeted matching preserved patches
showed no leading trivial guard plus multi-instance numeric body executor.

Proposed CP-first strict7 fixture uses unrelated names, argument routes, class
guard and instance-shadow mutations, guard/field descriptors, main/nested code
replacement, custom lookup/Int64/exposed dictionary fallback, exact exception+
traceback, and original profile/trace frames. Output is unobserved. Root may
register unchanged draft as core/two_argument_numeric_ir.py and matching expected
file. No registrations/validation harness are authored here.

Eligibility still requires an observer-free route proof. Existing --perf-counters
could compare this method route against the same saved unbound Python method
if ModuleValue/frame counts distinguish them; no new counters/private mock are
proposed. Current matched native arithmetic locations could establish execution,
without CPU-share inference. If neither distinguishes support, keep trial held.

Root alone preserves/applies/builds/tests. CP-first fixture, independent review,
meaningful admission evidence, fresh full correctness/all55CTest/API2, unchanged
default11gate21/5/.10 and paired original raytrace decide acceptance. Predeclare
usefulness before timers. Uncached IR scan/6class lookups/linear attrs can outweigh
the removed frame and affect unrelated gates; no gain is predicted. No author
runtime/Python/AST/build/test/timing execution or live engine/Git mutation.
