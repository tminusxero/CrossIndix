"""The converter end to end with the Lipi builder: one size of Noto Serif
Devanagari with --shape auto yields a CPFONT file at the current version and
the log reports the shaping table. Skipped without uharfbuzz/freetype or the
font (LIPI_FONT_DIR, as for lipi/builder/test_shaping.py)."""
import os
import struct
import subprocess
import sys

import pytest

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
pytest.importorskip("uharfbuzz")
pytest.importorskip("freetype")
from cpfont_version import CPFONT_VERSION  # noqa: E402

FONT_DIR = os.environ.get("LIPI_FONT_DIR", "")
SYSTEM_FONT_DIRS = ("/usr/share/fonts/truetype/noto",)


def _font(names):
    for directory in (FONT_DIR,) + SYSTEM_FONT_DIRS:
        for name in names:
            path = os.path.join(directory, name)
            if directory and os.path.exists(path):
                return path
    pytest.skip(f"set LIPI_FONT_DIR to a directory holding {names[0]}")


def test_fontconvert_cli_writes_a_v5_font_with_a_cluster_table(tmp_path):
    path = _font(("NotoSerifDevanagari[wdth,wght].ttf", "NotoSerifDevanagari.ttf"))
    out = subprocess.run([sys.executable, os.path.join(SCRIPT_DIR, "fontconvert_sdcard.py"), "--regular", path,
                          "--name", "ProbeDeva", "--sizes", "10", "--intervals", "reading,devanagari",
                          "--shape", "auto", "--output-dir", str(tmp_path)], capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    assert "consonant+virama" in out.stdout + out.stderr
    files = sorted(tmp_path.rglob("ProbeDeva_10.cpfont"))
    assert len(files) == 1
    data = files[0].read_bytes()
    assert data[:6] == b"CPFONT" and struct.unpack_from("<H", data, 8)[0] == CPFONT_VERSION
    assert len(data) > 50_000


def test_shaping_forms_take_the_fonts_pair_kerning(tmp_path):
    """Noto Serif Devanagari closes a half form up to the next consonant through
    its kern/dist pairs (न्य: न्.half -60 units). The half form is a PUA glyph
    with no cmap entry; the converter exports its pairs through the edge glyphs
    and takes the probe letter's pair off the advance the builder baked in, so
    the device lands य where HarfBuzz does and the pairs do not count twice."""
    import uharfbuzz as hb
    from fontTools.ttLib import TTFont
    path = _font(("NotoSerifDevanagari[wdth,wght].ttf", "NotoSerifDevanagari.ttf"))
    out = subprocess.run([sys.executable, os.path.join(SCRIPT_DIR, "fontconvert_sdcard.py"), "--regular", path,
                          "--name", "KernDeva", "--sizes", "16", "--intervals", "reading,devanagari",
                          "--shape", "auto", "--output-dir", str(tmp_path)], capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    tools = os.path.join(SCRIPT_DIR, "..", "..", "..", "lipi", "tools")
    sys.path.insert(0, tools)
    import hb_parity  # noqa: E402
    import render  # noqa: E402
    cpfont = str(next(tmp_path.rglob("KernDeva_16.cpfont")))
    table, _spec = hb_parity.load_table(cpfont)
    font = render.load_cpfont(cpfont)
    half_na = next(cp for cp, key in table.items() if key == (0x0928, 0x094D, 0x200D))
    scale = 16 * 150 / 72 / TTFont(path)["head"].unitsPerEm
    hbfont = hb.Font(hb.Face(hb.Blob.from_file_path(path)))
    buf = hb.Buffer()
    buf.add_str("न्य")
    buf.guess_segment_properties()
    hb.shape(hbfont, buf, {})
    name = hbfont.glyph_to_string(buf.glyph_infos[0].codepoint)
    hmtx = TTFont(path)["hmtx"][name][0]
    # Device: advance + pair kern (both 1/16 px) = HarfBuzz's advance, within rounding.
    device_fp = font["glyph"](half_na)["adv"] + font["kern"](half_na, 0x092F)
    assert abs(device_fp - buf.glyph_positions[0].x_advance * scale * 16) <= 2
    assert abs(font["glyph"](half_na)["adv"] - hmtx * scale * 16) <= 2  # the probe pair is off the advance
    assert font["kern"](half_na, 0x092F) < 0


def test_pair_kerning_takes_the_first_matching_subtable(tmp_path):
    """A GPOS lookup's subtables are alternatives: HarfBuzz applies the first
    one that matches a pair and skips the rest, while the converter used to add
    them up (Noto Serif Bengali: 9 pairs sit in two subtables of one lookup, so
    ে before the ন্দ্ব composite came out 1.2 px too tight). The device's kern
    for every pair the font lists in more than one subtable must equal what
    HarfBuzz applies, the sum being wrong for all of them."""
    import uharfbuzz as hb
    from fontTools.ttLib import TTFont
    path = _font(("NotoSerifBengali[wdth,wght].ttf", "NotoSerifBengali.ttf"))
    out = subprocess.run([sys.executable, os.path.join(SCRIPT_DIR, "fontconvert_sdcard.py"), "--regular", path,
                          "--name", "KernBeng", "--sizes", "16", "--intervals", "reading,bengali",
                          "--shape", "auto", "--output-dir", str(tmp_path)], capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    tools = os.path.join(SCRIPT_DIR, "..", "..", "..", "lipi", "tools")
    sys.path.insert(0, tools)
    import hb_parity  # noqa: E402
    import render  # noqa: E402
    cpfont = str(next(tmp_path.rglob("KernBeng_16.cpfont")))
    table, _spec = hb_parity.load_table(cpfont)
    font = render.load_cpfont(cpfont)
    tt = TTFont(path)
    scale = 16 * 150 / 72 / tt["head"].unitsPerEm
    # The font's pair (uni09C7, uni09A809CD09A609CD09AC) is -60 in the lookup's
    # first (pair) subtable and -35 in its second (class) subtable; the conjunct
    # glyph has no cmap entry, so the device reaches the pair through the ন্দ্ব
    # composite's edge glyph.
    e_sign = 0x09C7
    ndb = next(cp for cp, key in table.items() if key == (0x09A8, 0x09CD, 0x09A6, 0x09CD, 0x09AC))
    hbfont = hb.Font(hb.Face(hb.Blob.from_file_path(path)))
    buf = hb.Buffer()
    buf.add_str("কন্দ্বে")  # after ক, so the sign keeps its plain (not word-initial) glyph
    buf.guess_segment_properties()
    hb.shape(hbfont, buf, {})
    names = [hbfont.glyph_to_string(i.codepoint) for i in buf.glyph_infos]
    assert names[1] == "uni09C7" and names[2] == "uni09A809CD09A609CD09AC", names
    hb_kern = buf.glyph_positions[1].x_advance - tt["hmtx"]["uni09C7"][0]
    assert hb_kern != 0
    device_fp = font["kern"](e_sign, ndb)
    assert abs(device_fp - hb_kern * scale * 16) <= 2, (device_fp, hb_kern)
    # The two subtables' values added up (the old reading) is not what the font applies.
    gpos = tt["GPOS"].table
    values = []
    for st in gpos.LookupList.Lookup[0].SubTable:
        if getattr(st, "ExtSubTable", None) is not None:
            st = st.ExtSubTable
        if st.Format == 1:
            for g, ps in zip(st.Coverage.glyphs, st.PairSet):
                if g != "uni09C7":
                    continue
                values += [r.Value1.XAdvance for r in ps.PairValueRecord if r.SecondGlyph == names[2]]
        else:
            c1, c2 = st.ClassDef1.classDefs, st.ClassDef2.classDefs
            if "uni09C7" in st.Coverage.glyphs:
                rec = st.Class1Record[c1.get("uni09C7", 0)].Class2Record[c2.get(names[2], 0)]
                values.append(rec.Value1.XAdvance if rec.Value1 else 0)
    assert len(values) >= 2 and values[0] == hb_kern and sum(values) != hb_kern, values
    assert abs(device_fp - sum(values) * scale * 16) > 2


def test_shaped_script_without_uharfbuzz_is_an_error(tmp_path):
    """A shaped script found in the intervals (--shape auto, the default the
    font manifest relies on) needs the builder's uharfbuzz; without it the
    converter used to warn and write a font with no cluster table, which
    loads fine and draws every conjunct as consonant + hasanta. It must fail
    before writing anything. The stub module on PYTHONPATH stands in for the
    missing package; the Latin fixture never gets rasterised."""
    stub = tmp_path / "stub"
    stub.mkdir()
    (stub / "uharfbuzz.py").write_text("raise ImportError('stub: uharfbuzz is not installed')\n")
    env = dict(os.environ)
    env["PYTHONPATH"] = str(stub) + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    ttf = os.path.join(SCRIPT_DIR, "..", "builtinFonts", "source", "ChareInk7", "ChareInk7-Regular.ttf")
    out_dir = tmp_path / "out"
    out = subprocess.run([sys.executable, os.path.join(SCRIPT_DIR, "fontconvert_sdcard.py"), "--regular", ttf,
                          "--name", "NoHb", "--sizes", "10", "--intervals", "reading,devanagari",
                          "--shape", "auto", "--output-dir", str(out_dir)], capture_output=True, text=True, env=env)
    assert out.returncode == 1, out.stderr
    assert "uharfbuzz" in out.stderr and "--shape none" in out.stderr
    assert not out_dir.exists() or not list(out_dir.rglob("*.cpfont"))
