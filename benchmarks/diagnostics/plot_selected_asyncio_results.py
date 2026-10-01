"""Render saved official asyncio results as horizontal elapsed-time bars.

Requires reportlab. Optional PNG preview also requires pypdfium2. TCP/TLS and
WebSockets identify their separate measured builds in the chart footnote.
"""
import argparse
import json
from pathlib import Path
import statistics

from reportlab.graphics import renderPDF, renderSVG
from reportlab.graphics.charts.barcharts import HorizontalBarChart
from reportlab.graphics.shapes import Drawing, Rect, String
from reportlab.lib.colors import HexColor


ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "doc/performance/data"


def means(filename):
    document = json.loads((DATA / filename).read_text(encoding="utf-8"))
    result = {}
    for benchmark in document["benchmarks"]:
        name = benchmark.get("metadata", {}).get(
            "name", document.get("metadata", {}).get("name"))
        values = [value for run in benchmark["runs"] for value in run.get("values", [])]
        if name and values:
            result[name] = statistics.mean(values)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    cp = means("iocp-official-cpython314-fast-20261001.json")
    cp.update(means("iocp-websockets-cpython314-fast-20261001.json"))
    xl = means("iocp-official-native-pending-fast-20261001.json")
    xl.update(means("websockets-lazy-module-native-pending-fast-20261001.json"))
    names = ["asyncio_websockets", "asyncio_tcp_ssl", "asyncio_tcp"]
    ratios = [xl[name] / cp[name] for name in names]
    drawing = Drawing(1000, 380)
    drawing.add(Rect(0, 0, 1000, 380, fillColor=HexColor("#ffffff"), strokeColor=None))
    drawing.add(String(25, 350, "Selected official asyncio results", fontName="Helvetica-Bold", fontSize=21))
    drawing.add(String(25, 324, "Elapsed time relative to CPython 3.14.7 (1x); shorter is faster.", fontSize=12))
    chart = HorizontalBarChart()
    chart.x, chart.y, chart.width, chart.height = 185, 130, 695, 160
    chart.data = [[1.0] * len(names), ratios]
    chart.categoryAxis.categoryNames = ["WebSockets", "TLS", "TCP"]
    chart.categoryAxis.labels.fontSize = 11
    chart.valueAxis.valueMin, chart.valueAxis.valueMax = 0, 8.5
    chart.valueAxis.valueSteps = [0, 1, 2, 4, 6, 8]
    chart.valueAxis.labelTextFormat = lambda value: f"{value:g}x"
    chart.valueAxis.labels.fontSize = 10
    chart.barWidth, chart.barSpacing, chart.groupSpacing = 13, 4, 14
    chart.bars[0].fillColor = HexColor("#64748b")
    chart.bars[1].fillColor = HexColor("#b45309")
    chart.bars.strokeColor = None
    chart.barLabelFormat = lambda value: f"{value:.2f}x"
    chart.barLabels.nudge = 8
    chart.barLabels.fontSize = 10
    drawing.add(chart)
    for x, color, text in [(190, "#64748b", "CPython 3.14.7"), (390, "#b45309", "XLang3")]:
        drawing.add(Rect(x, 93, 15, 10, fillColor=HexColor(color), strokeColor=None))
        drawing.add(String(x + 23, 93, text, fontSize=11))
    drawing.add(String(25, 65, "Fast-mode means; these are three selected cases, not a full-suite score.", fontSize=10))
    drawing.add(String(25, 45, "XLang3 runtime SHA prefixes: TCP/TLS 87200865; WebSockets F1804355 (lazy keyword export fix).", fontSize=10))
    drawing.add(String(25, 25, "Neither TCP/TLS nor WebSockets shows a statistically significant speedup over its XLang3 control.", fontSize=10))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    renderSVG.drawToFile(drawing, str(args.output))
    if args.preview:
        import pypdfium2 as pdfium
        with pdfium.PdfDocument(renderPDF.drawToString(drawing)) as document:
            document[0].render(scale=120 / 72).to_pil().save(str(args.preview))
    for name, ratio in zip(names, ratios):
        print(f"{name}: CPython={cp[name]:.9f}s XLang3={xl[name]:.9f}s {ratio:.4f}x slower")


if __name__ == "__main__":
    main()
