"""Run pyperformance's raytrace body once so XLang3 can count VM operations."""

from run_benchmark import bench_raytrace


bench_raytrace(1, 100, 100, None)
