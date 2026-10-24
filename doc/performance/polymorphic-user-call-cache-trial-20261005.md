# Polymorphic Python-function call-site trial (2026-10-05)

## Question and implementation

`pickle.py`'s pure-Python `_Pickler.save` calls a function from its type
dispatch table as `f(self, obj)`. The target can change between ordinary
Python functions at the same generic `Call` instruction. XLang3's generic
call-site cache guards an exact function object and, on each target miss,
re-enters its Python-function specialization analysis.

The trial promoted a call site to a generic Python-function path after three
target changes with the same positional argument count. It retained the
ordinary binder, frame push, tracing, and generator behavior, and stopped
retaining a specific function object at a polymorphic site. This changed the
XLang3 interpreter only; no Python standard-library implementation changed.

## Result

The official pyperformance 1.14.0 `pickle_pure_python` rigorous candidate
measured **5.30 ms ± 0.56 ms**. Its pre-trial XLang3 Release control measured
**5.20 ms ± 0.07 ms**. `pyperf compare_to` reported the candidate **1.02×
slower** (`t = -2.03`). The candidate's maximum was 6.63 ms, substantially
above its mean; this run was noisy, but it gives no evidence of a gain. The
trial was rejected and its interpreter changes and focused fixture were
removed.

The suspected dispatch site was real, but bypassing its exact-target cache
analysis did not reduce end-to-end time. Do not retry this fallback unchanged.
The XLang3 build was restored to the checked-in source, and full Release CTest
passed **55/55** after restoration. The experimental fixture passed before
being removed. No fixed Release gate was run because the candidate was
rejected.

## Evidence

- [Pre-trial Release control](data/pyperformance-xlang3-callglobal-control-pickle-rigorous-20261005.json)
- [Polymorphic-call candidate](data/pyperformance-xlang3-polymorphic-call-pickle-candidate-r1-rigorous-20261005.json) and [runner log](data/pyperformance-xlang3-polymorphic-call-pickle-candidate-r1-rigorous-20261005.log)
- [pyperf comparison table](data/polymorphic-call-pickle-compare-20261005.txt)
- [pyperf significance output](data/polymorphic-call-pickle-significance-20261005.txt)
- [Python 3.14.7 Pickler IR dump](data/pickle-ir-20261004/pickle.ir.txt), function `_Pickler.save`

Both benchmark runs used pyperformance **1.14.0**, pyperf **2.10.0**,
CPython **3.14.7** as the benchmark manager, and the shared dependency site.
The control was the same-day pre-trial Release build; the intervening pushed
checkpoint changed JSON parsing only, not the Pickler or generic `Call` path.
The candidate worker used the repository's Windows compatibility shim.
