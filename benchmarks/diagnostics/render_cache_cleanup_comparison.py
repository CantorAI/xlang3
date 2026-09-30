"""Render the retained rigorous unpickle pair and CPython reference.

Requires ReportLab. Generates the checked-in SVG and a scratch PDF for visual
review; neither artifact is a new measurement or a complete suite comparison.
"""

import json
from pathlib import Path
import statistics

from reportlab.graphics.charts.barcharts import HorizontalBarChart
from reportlab.graphics.shapes import Drawing, Line, String
from reportlab.graphics import renderPDF, renderSVG
from reportlab.lib.colors import HexColor

root = Path(__file__).resolve().parents[2]
data = root / "doc/performance/data"
specs = [
    ("CPython 3.14.7", "cpython314-rigorous", "#2879bd"),
    ("XLang3 previous runtime", "global-parent-rigorous", "#64748b"),
    ("XLang3 retained runtime", "global-candidate-rigorous", "#0f8d76"),
]
scores = []
for label, stem, color in specs:
    result = json.loads((data / f"unpickle-domain-cleanup-{stem}-20260930.json").read_text())
    values = [value * 1000 for run in result["benchmarks"][0]["runs"] for value in run.get("values", [])]
    assert len(values) == 120
    scores.append((label, statistics.mean(values), statistics.stdev(values), color))

drawing = Drawing(1120, 500)
drawing.add(String(35, 464, "Pure-Python unpickle: XLang3 vs CPython 3.14", fontName="Helvetica-Bold", fontSize=24))
drawing.add(String(35, 434, "Unchanged official pyperformance workload / protocol 5 / rigorous Release runs", fontSize=13, fillColor=HexColor("#475569")))
chart = HorizontalBarChart()
chart.x, chart.y, chart.width, chart.height = 300, 130, 720, 280
ordered = list(reversed(scores))
chart.data = [[s[1] for s in ordered]]
chart.categoryAxis.categoryNames = [s[0] for s in ordered]
chart.categoryAxis.labels.fontSize = 13
chart.categoryAxis.labels.dx = -8
chart.categoryAxis.visibleTicks = False
chart.categoryAxis.visibleAxis = False
chart.valueAxis.valueMin, chart.valueAxis.valueMax, chart.valueAxis.valueStep = 0, 5, 1
chart.valueAxis.labels.fontSize = 12
chart.valueAxis.visibleGrid = True
chart.valueAxis.gridStrokeColor = HexColor("#dbe3ec")
chart.groupSpacing, chart.barSpacing = 18, 0
chart.bars.strokeColor = None
for index, (_, _, _, color) in enumerate(ordered):
    chart.bars[(0, index)].fillColor = HexColor(color)
drawing.add(chart)
for index, (_, mean, stdev, _) in enumerate(ordered):
    y = chart.y + chart.height / len(scores) * (index + .5)
    left = chart.x + (mean - stdev) / 5 * chart.width
    right = chart.x + (mean + stdev) / 5 * chart.width
    drawing.add(Line(left, y, right, y, strokeColor=HexColor("#1e293b")))
    for x in (left, right):
        drawing.add(Line(x, y-4, x, y+4, strokeColor=HexColor("#1e293b")))
    drawing.add(String(right + 9, y - 4, f"{mean:.3f} ±{stdev:.3f}", fontSize=12))
drawing.add(String(300, 87, "Mean time per benchmark unit (ms) - shorter is faster", fontSize=14))
drawing.add(String(35, 47, "Retained XLang3 change: 1.02x faster than its previous runtime. Error bars: standard deviation.", fontSize=12, fillColor=HexColor("#475569")))
drawing.add(String(35, 26, "XLang3 remains 23.49x slower than CPython on this case. This is not the full suite.", fontSize=12, fillColor=HexColor("#475569")))
renderSVG.drawToFile(drawing, str(root / "doc/performance/unpickle-cache-domain-cleanup-vs-cpython314-20260930.svg"))
pdf = root / "scratch/unpickle-cache-domain-cleanup-vs-cpython314-20260930.pdf"
renderPDF.drawToFile(drawing, str(pdf))
print("chart saved")
