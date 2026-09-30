import math


for function in (math.isfinite, math.isinf, math.isnan):
    for value in ("2", None, object()):
        try:
            function(value)
        except Exception as exc:
            print(function.__name__, type(value).__name__, type(exc).__name__, str(exc))
