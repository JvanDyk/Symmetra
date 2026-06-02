#!/usr/bin/env python3
"""
FIRST LIGHT 3D Font Generator
Generates a TTF font file with 3D letter geometry
"""

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
import sys

def create_letter_outline(char_code):
    """Create a simple geometric outline for each letter"""
    outlines = {
        ord('F'): [(100, 0), (600, 0), (600, 150), (350, 150), (350, 450), (600, 450), (600, 600), (100, 600), (100, 450), (350, 450), (350, 150), (100, 150), (100, 0)],
        ord('I'): [(150, 0), (250, 0), (250, 600), (150, 600), (150, 0)],
        ord('R'): [(100, 0), (600, 0), (600, 150), (350, 150), (350, 300), (600, 300), (100, 600), (100, 450), (350, 450), (350, 150), (100, 150), (100, 0)],
        ord('S'): [(550, 50), (150, 50), (150, 200), (450, 200), (150, 350), (150, 550), (550, 550), (550, 400), (250, 400), (550, 250), (550, 50)],
        ord('T'): [(50, 0), (600, 0), (600, 150), (400, 150), (400, 600), (250, 600), (250, 150), (50, 150), (50, 0)],
        ord('L'): [(100, 0), (250, 0), (250, 450), (550, 450), (550, 600), (100, 600), (100, 0)],
        ord('G'): [(600, 50), (200, 50), (200, 550), (600, 550), (600, 400), (350, 400), (350, 300), (600, 300), (600, 50)],
        ord('H'): [(100, 0), (250, 0), (250, 300), (500, 300), (500, 0), (650, 0), (650, 600), (500, 600), (500, 450), (250, 450), (250, 600), (100, 600), (100, 0)],
    }

    return outlines.get(char_code, [(100, 0), (500, 0), (500, 600), (100, 600), (100, 0)])

def generate_ttf():
    """Generate a proper TTF font"""
    print("[*] Generating FIRST LIGHT Font...")

    # Initialize font builder
    fb = FontBuilder(1000)

    # Define glyphs
    glyphs = {}
    metrics = {}

    # Add .notdef
    pen = TTGlyphPen(glyphSet=None)
    pen.moveTo((100, 0))
    pen.lineTo((500, 0))
    pen.lineTo((500, 700))
    pen.lineTo((100, 700))
    pen.closePath()
    glyphs['.notdef'] = pen.glyph()
    metrics['.notdef'] = (600, 50)

    # Add letters: F, I, R, S, T, L, G, H
    chars_to_add = [ord('F'), ord('I'), ord('R'), ord('S'), ord('T'), ord('L'), ord('G'), ord('H'), ord(' ')]

    for char_code in chars_to_add:
        char = chr(char_code)

        if char == ' ':
            # Space glyph - empty outline
            pen = TTGlyphPen(glyphSet=None)
            glyphs['space'] = pen.glyph()
            metrics['space'] = (300, 0)
        else:
            # Get letter outline
            points = create_letter_outline(char_code)

            # Create pen and draw the glyph
            pen = TTGlyphPen(glyphSet=None)

            if points:
                pen.moveTo(points[0])
                for point in points[1:]:
                    pen.lineTo(point)
                pen.closePath()

            glyphs[char] = pen.glyph()

            # Set metrics (width, left bearing)
            if char == 'I':
                metrics[char] = (300, 30)
            elif char in ['S']:
                metrics[char] = (600, 50)
            else:
                metrics[char] = (700, 50)

    # Setup glyph order
    glyph_order = ['.notdef', 'space', 'F', 'I', 'R', 'S', 'T', 'L', 'G', 'H']
    fb.setupGlyphOrder(glyph_order)

    # Setup character map
    cmap = {}
    cmap[ord(' ')] = 'space'
    cmap[ord('F')] = 'F'
    cmap[ord('I')] = 'I'
    cmap[ord('R')] = 'R'
    cmap[ord('S')] = 'S'
    cmap[ord('T')] = 'T'
    cmap[ord('L')] = 'L'
    cmap[ord('G')] = 'G'
    cmap[ord('H')] = 'H'

    fb.setupCharacterMap(cmap)

    # Setup glyf table
    fb.setupGlyf(glyphs)

    # Setup horizontal metrics
    fb.setupHorizontalMetrics(metrics)

    # Setup head table
    fb.setupHead(unitsPerEm=1000)

    # Setup hhea table
    fb.setupHorizontalHeader(ascent=800, descent=-200)

    # Setup maxp table
    fb.setupMaxp()

    # Setup name table
    nameStrings = {
        'familyName': 'First Light',
        'styleName': 'Regular',
    }
    fb.setupNameTable(nameStrings)

    # Setup OS/2 table
    fb.setupOS2()

    # Setup post table
    fb.setupPost()

    # Save the font
    filename = 'FirstLight.ttf'
    fb.save(filename)

    print(f"[+] Font generated successfully: {filename}")
    return filename

if __name__ == "__main__":
    try:
        output = generate_ttf()
        print(f"\n[SUCCESS] Font file created: {output}")
        print("[INFO] You can now use this font in your projects!")
    except Exception as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)
