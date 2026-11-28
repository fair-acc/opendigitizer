"""Generates od-glyphs.ttf, OpenDigitizer's own glyph font: processor glyphs for CPU, GPU, FPGA and TPU, as compact
silhouettes and inside a chip package.

Each glyph is drawn on a 16 x 16 unit grid (64 font units per grid unit), mostly at whole units, so it renders sharp
at 16 px and stays legible down to 12 px; rounded corners are quadratic arcs. The generated font is checked in; run this only when a glyph changes:

    python3 -m venv /tmp/fontvenv && /tmp/fontvenv/bin/pip install fonttools
    /tmp/fontvenv/bin/python src/ui/assets/odglyphs/generate_od_glyphs.py

The codepoints are those of src/ui/common/ProcessorGlyphs.hpp, in the Private Use Area.
"""

import math
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

UNIT = 64  # font units per grid unit
EM = 16 * UNIT
DESCENT = (
    2 * UNIT
)  # the grid's bottom sits this far below the baseline, as an icon font's glyphs do


def polygon(points):
    """a closed polygon in grid units, its points in the order given"""
    return [("move", points[0])] + [("line", point) for point in points[1:]]


def rect(x0, y0, x1, y1, r=0.0, hole=False):
    """a rectangle in grid units with corners rounded by `r` (quadratic arcs); clockwise fills, a `hole` runs the other way"""
    if r == 0.0:
        corners = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]
        return polygon(list(reversed(corners)) if hole else corners)
    if hole:
        return [
            ("move", (x0 + r, y0)),
            ("line", (x1 - r, y0)),
            ("q", (x1, y0), (x1, y0 + r)),
            ("line", (x1, y1 - r)),
            ("q", (x1, y1), (x1 - r, y1)),
            ("line", (x0 + r, y1)),
            ("q", (x0, y1), (x0, y1 - r)),
            ("line", (x0, y0 + r)),
            ("q", (x0, y0), (x0 + r, y0)),
        ]
    return [
        ("move", (x0, y0 + r)),
        ("line", (x0, y1 - r)),
        ("q", (x0, y1), (x0 + r, y1)),
        ("line", (x1 - r, y1)),
        ("q", (x1, y1), (x1, y1 - r)),
        ("line", (x1, y0 + r)),
        ("q", (x1, y0), (x1 - r, y0)),
        ("line", (x0 + r, y0)),
        ("q", (x0, y0), (x0, y0 + r)),
    ]


def bar(x0, y0, x1, y1, width):
    """a straight bar of `width` grid units from (x0, y0) to (x1, y1), clockwise"""
    length = math.hypot(x1 - x0, y1 - y0)
    nx, ny = -(y1 - y0) / length * width / 2, (x1 - x0) / length * width / 2
    return polygon(
        [(x0 - nx, y0 - ny), (x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny)]
    )


def cpu():
    """a die: a thin rounded outline round its core, three pins per side"""
    contours = [
        rect(3, 3, 13, 13, 1.5),
        rect(4, 4, 12, 12, 0.5, hole=True),
        rect(6, 6, 10, 10, 0.5),
    ]
    for a, b in ((5, 6), (7.5, 8.5), (10, 11)):
        contours += [
            rect(a, 13, b, 15),
            rect(a, 1, b, 3),
            rect(1, a, 3, b),
            rect(13, a, 15, b),
        ]
    return contours


def gpu():
    """three by three compute cells"""
    return [rect(x, y, x + 4, y + 4, 0.75) for x in (1, 6, 11) for y in (1, 6, 11)]


def fpga():
    """four logic pads, routed along the edges and across"""
    pads = [rect(x, y, x + 4, y + 4, 0.75) for x in (1, 11) for y in (1, 11)]
    edges = [
        rect(5, 2, 11, 3),
        rect(5, 13, 11, 14),
        rect(2, 5, 3, 11),
        rect(13, 5, 14, 11),
    ]
    return pads + edges + [bar(4, 4, 12, 12, 1.5), bar(4, 12, 12, 4, 1.5)]


def tpu():
    """a systolic array: three by three multiply cells on the lines that carry its operands, pinned out on every side"""
    lines = [rect(c, 0.5, c + 1, 14.5) for c in (3, 7, 11)] + [
        rect(0.5, c, 14.5, c + 1) for c in (3, 7, 11)
    ]
    cells = [
        rect(c - 0.75, r - 0.75, c + 1.75, r + 1.75, 1.25)
        for c in (3, 7, 11)
        for r in (3, 7, 11)
    ]
    return lines + cells


def package(motif):
    """a chip package round `motif`: a thin rounded outline with three pins per side"""
    contours = [rect(2, 2, 14, 14, 1.5), rect(3, 3, 13, 13, 0.75, hole=True)]
    for a, b in ((4.5, 5.5), (7.5, 8.5), (10.5, 11.5)):
        contours += [
            rect(a, 14, b, 15.5),
            rect(a, 0.5, b, 2),
            rect(0.5, a, 2, b),
            rect(14, a, 15.5, b),
        ]
    return contours + motif


def cpuOutline():
    """the die inside its package"""
    return package([rect(5, 5, 11, 11, 0.5), rect(6.5, 6.5, 9.5, 9.5, 0.25, hole=True)])


def gpuOutline():
    """three by three compute cells inside their package"""
    return package(
        [rect(x, y, x + 2, y + 2, 0.3) for x in (4.5, 7, 9.5) for y in (4.5, 7, 9.5)]
    )


def fpgaOutline():
    """logic pads routed along the edges and across, inside their package"""
    pads = [rect(x, y, x + 2, y + 2, 0.3) for x in (4.5, 9.5) for y in (4.5, 9.5)]
    edges = [
        rect(6.5, 5.25, 9.5, 5.75),
        rect(6.5, 10.25, 9.5, 10.75),
        rect(5.25, 6.5, 5.75, 9.5),
        rect(10.25, 6.5, 10.75, 9.5),
    ]
    return package(pads + edges + [bar(6, 6, 10, 10, 0.75), bar(6, 10, 10, 6, 0.75)])


def tpuOutline():
    """a systolic array inside its package"""
    lines = [rect(c - 0.3, 4.5, c + 0.3, 11.5) for c in (5.5, 8, 10.5)] + [
        rect(4.5, c - 0.3, 11.5, c + 0.3) for c in (5.5, 8, 10.5)
    ]
    cells = [
        rect(c - 0.75, r - 0.75, c + 0.75, r + 0.75, 0.75)
        for c in (5.5, 8, 10.5)
        for r in (5.5, 8, 10.5)
    ]
    return package(lines + cells)


# B: compact silhouettes; A: the same inside a chip package
GLYPHS = {
    "cpu": (0xE000, cpu),
    "gpu": (0xE001, gpu),
    "fpga": (0xE002, fpga),
    "tpu": (0xE003, tpu),
    "cpu.outline": (0xE004, cpuOutline),
    "gpu.outline": (0xE005, gpuOutline),
    "fpga.outline": (0xE006, fpgaOutline),
    "tpu.outline": (0xE007, tpuOutline),
}


def outline(contours):
    pen = TTGlyphPen(None)
    at = lambda point: (round(point[0] * UNIT), round(point[1] * UNIT) - DESCENT)
    for contour in contours:
        for kind, *points in contour:
            if kind == "move":
                pen.moveTo(at(points[0]))
            elif kind == "line":
                pen.lineTo(at(points[0]))
            else:
                pen.qCurveTo(*map(at, points))
        pen.closePath()
    return pen.glyph()


def build(path):
    names = [".notdef", *GLYPHS]
    builder = FontBuilder(EM, isTTF=True)
    builder.setupGlyphOrder(names)
    builder.setupCharacterMap(
        {codepoint: name for name, (codepoint, _) in GLYPHS.items()}
    )
    builder.setupGlyf(
        {
            ".notdef": outline([]),
            **{name: outline(draw()) for name, (_, draw) in GLYPHS.items()},
        }
    )
    builder.setupHorizontalMetrics({name: (EM, 0) for name in names})
    builder.setupHorizontalHeader(ascent=EM - DESCENT, descent=-DESCENT)
    builder.setupNameTable(
        {
            "familyName": "OpenDigitizer Glyphs",
            "styleName": "Regular",
            "copyright": "Copyright (C) 2026 the OpenDigitizer authors",
            "licenseDescription": "GNU Lesser General Public License, version 3 or later",
            "version": "Version 1.0",
        }
    )
    builder.setupOS2(
        sTypoAscender=EM - DESCENT,
        sTypoDescender=-DESCENT,
        usWinAscent=EM - DESCENT,
        usWinDescent=DESCENT,
    )
    builder.setupPost()
    builder.save(path)


if __name__ == "__main__":
    build(Path(__file__).with_name("od-glyphs.ttf"))
