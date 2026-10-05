---
title: SD Card Fonts
nav_order: 4
---

# SD Card Fonts

CrossInk supports loading additional fonts from the SD card, including fonts
with extended Unicode coverage (CJK, Cyrillic, Greek, etc.).

## Installing Fonts

There are three ways to install fonts:

### Option 1: Download from device

1. Connect your CrossInk reader to Wi-Fi
2. Go to **Settings > Reader > Font Options > Manage Fonts**
3. Browse available font families and select to download
4. Downloaded fonts appear immediately in **Settings > Reader > Font Options > Font Family**

**Note**: To change the font sizes that are downloaded, change the option for `Download Font Size Range` _before_ downloading.

### Option 2: Upload via web browser

1. Start **File Transfer** and connect through **Join Network** or **Create Hotspot**
2. Open the web interface URL shown on the reader
3. Navigate to the **Fonts** tab
4. Upload `.cpfont` files using the upload form

### Option 3: Manual SD card copy (Fastest)

1.  Download font files from the
    [CrossInk Fonts](https://github.com/uxjulia/crossink-fonts/tree/main/cpfonts) repository.
    - Click the `.zip` file for the font you want then click on the download icon to download the raw file.
2.  Copy font family folders to one of two locations on your SD card:
    - `/.fonts/` — hidden directory (preferred; keeps the SD root tidy
      when mounted on a desktop)
    - `/fonts/` — visible directory (use this if your OS hides dot-files
      and you'd rather see the folder in your file manager)

    Both roots are always scanned at boot and the results are merged: a
    family installed in `/fonts/` shows up even when `/.fonts/` also
    exists, and vice versa. The two roots only collide if the same family
    name appears in both — in that case the copy in `/.fonts/` wins and
    the duplicate in `/fonts/` is ignored.

        SD Card Root/
        ├── .fonts/                     ← Hidden root (preferred)
        │   └── Literata/
        │       ├── Literata_12.cpfont
        │       ├── Literata_14.cpfont
        │       ├── Literata_16.cpfont
        │       └── Literata_18.cpfont
        └── fonts/                      ← Visible root (equally valid)
            └── Merriweather/
                ├── Merriweather_12.cpfont
                └── ...

3.  Insert the SD card and power on your CrossInk device

## Dictionary Fonts

EPUB books can use a different installed SD-card family for dictionary definitions.
This can be set globally or per-book via `Font Options`. If a
saved point size is no longer available, CrossInk chooses the closest file from
the dictionary family. If the device experiences low available RAM, you may see the
dictionary font fall back to your reader font. This is normal.

### Generating dictionary font families

Use the dictionary-specific builder to generate the complete family catalog with
the extra coverage used by dictionary definitions:

    python3 -m pip install -r lib/EpdFont/scripts/requirements.txt
    python3 lib/EpdFont/scripts/build-dictionary-fonts.py \
      --output-dir ./generated-dictionary-fonts \
      --clean \
      --jobs 2

The dictionary build includes the `reading` ranges and the built-in ranges, plus
IPA and phonetic-extension characters (`U+0250–U+02FF` and `U+1D00–U+1DBF`) and
combining-mark ranges (`U+1DC0–U+1DFF`, `U+20D0–U+20FF`, and
`U+FE20–U+FE2F`).

The default output is `../crossink-fonts/dictionary-fonts`. Use a separate
`--output-dir` for personal builds, because `--clean` removes the selected output
directory before generating the fonts. The output contains family folders and ZIP
archives; copy a family folder or unzip its archive into `/.fonts/` or `/fonts/`
on the SD card. Use `--only FamilyA,FamilyB` to generate selected families.

## UI fallback by script

The built-in UI fonts cover Latin, Cyrillic, Greek, Hebrew and Arabic. When a
file name, book title or list row contains a script they lack (Devanagari,
Bengali, Gurmukhi, Gujarati, Odia, Tamil, Telugu, Kannada, Malayalam, Sinhala,
Han, kana, Hangul), the firmware looks for an installed SD-card family that
covers that script and draws the whole string with it at the matching UI size.

- At boot, and whenever fonts are installed or removed, every family that
  ships an 8, 10 or 12 pt file is probed for one letter per script. The first
  family covering a script is used for it; the selected reader family always
  wins for the scripts it covers.
- The files are loaded on demand, the first time a string in that script is
  drawn, and only at the sizes the UI needs (8, 10 and 12 pt). The X3 and X4
  keep one such family resident at a time; the X4 Pro and X4 Classic keep
  three. The least recently used family is unloaded when the limit is hit.
- The reader font is not involved: a Bengali book name shows in Bengali even
  when the reader font is Latin-only, after sleep or restart, and after a
  cache clear (which still resets per-book settings, as before).
- Opening a book whose title needs one of these scripts with a reader font
  that cannot draw it switches the reader to the covering family for that
  session (the global setting is untouched).

For this to work a family must include the UI sizes: pass `--sizes
8,10,12,...` to the converter. Scripts without a shaper (everything but
Bengali today) draw plain glyphs without conjunct shaping.

## Available Pre-Built Fonts

You can view pre-built fonts available for download at [Inky](https://inky.crossink.dev/#downloads).

## Converting Custom Fonts with CrossPoint's Font Builder

To convert your own TrueType/OpenType fonts use CrossPoint's [Font Builder](https://crosspointreader.com/fonts)

## Converting Custom Fonts with Python

### Prerequisites

    pip install freetype-py fonttools uharfbuzz

The shaped scripts (Bengali, Devanagari) are built by the Lipi engine, a git
submodule at `lipi/`; the converter imports it at start-up for every run, so
a checkout without `git submodule update --init` fails before converting
anything, Latin fonts included.

(`uharfbuzz` is only needed for Bengali shaping, see below.)

### Single font (one style)

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyFont-Regular.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --style regular \
      --name MyFont \
      --output-dir ./MyFont/

### Multi-style font

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      --regular MyFont-Regular.ttf \
      --bold MyFont-Bold.ttf \
      --italic MyFont-Italic.ttf \
      --bolditalic MyFont-BoldItalic.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --name MyFont \
      --output-dir ./MyFont/

### Available Unicode interval presets

| Preset        | Coverage                                                                                                             |
| ------------- | -------------------------------------------------------------------------------------------------------------------- |
| `ascii`       | U+0020–U+007E (Basic Latin)                                                                                          |
| `latin1`      | U+0080–U+00FF (Latin-1 Supplement)                                                                                   |
| `latin-ext`   | European languages (Latin + Extended-A/B + punctuation + ligatures)                                                  |
| `greek`       | Greek + Extended Greek                                                                                               |
| `cyrillic`    | Cyrillic + Supplement                                                                                                |
| `hebrew`      | Hebrew + Alphabetic Presentation Forms                                                                               |
| `georgian`    | Georgian + Georgian Supplement                                                                                       |
| `armenian`    | Armenian                                                                                                             |
| `ethiopic`    | Ethiopic + Extended                                                                                                  |
| `vietnamese`  | Vietnamese subset (ơ/ư and combining marks)                                                                          |
| `punctuation` | General punctuation (U+2000–U+206F)                                                                                  |
| `cjk`         | CJK Unified Ideographs + Hiragana + Katakana + Fullwidth                                                             |
| `hangul`      | Korean Hangul syllables + Jamo + Compatibility Jamo                                                                  |
| `cherokee`    | Cherokee (historic + supplement block)                                                                               |
| `tifinagh`    | Tifinagh                                                                                                             |
| `bengali`     | Bengali/Bangla block + danda punctuation (conjuncts and vowel signs are pre-shaped, see below)                        |
| `devanagari`  | Devanagari block (Hindi, Marathi, Nepali, Sanskrit; half forms, reph and vowel signs are pre-shaped, see below)      |
| `symbols`     | Math, currency, arrows, box-drawing, misc symbols, dingbats                                                          |
| `reading`     | Literary fiction coverage: Latin, Greek, Cyrillic, math/symbol blocks, supplemental punctuation, and CJK quote marks |
| `builtin`     | Matches the firmware's built-in font conversion intervals                                                            |

Combine presets with commas: `--intervals latin-ext,greek,cyrillic`

You can also specify arbitrary Unicode ranges directly:
`--intervals latin-ext,(0x2100-0x214F)`

To list all presets with codepoint counts:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py --list-presets

### Additional options

`--force-autohint` — force FreeType's auto-hinter instead of the font's native hinting (useful when a font's built-in hints produce poor results at small sizes).

`--shape {auto,none,bengali,devanagari}` — script shaping to pre-compute into the font. `auto` (the default) enables shaping for the Indic script (Bengali or Devanagari) the intervals cover; a font may carry one shaped script.

### Bengali (Bangla)

Bengali needs OpenType shaping that the reader cannot run on the device:
consonants joined by a virama become conjunct glyphs (ক্ষ, ন্ত্র), a leading র
turns into a reph mark over the next consonant, and vowel signs attach
before, after, below or above the syllable. The converter therefore does the
shaping once, at build time, using HarfBuzz:

- every consonant cluster the font can form is rasterised as one composite
  glyph (with the font's own GPOS positioning baked in) and stored under a
  Private Use Area codepoint;
- a small cluster table (a few KB, kept in RAM while the font is loaded)
  maps logical codepoint sequences to those glyphs;
- on the device, the firmware splits words into syllables, moves the
  pre-base vowel signs (ি ে ৈ and the ে half of ো ৌ) in front of the
  syllable, resolves each cluster through the table, and draws the remaining
  marks (ু ূ ৃ, hasanta, candrabindu, reph) as overlays on their base.

Any OpenType Bengali font works; Noto Serif Bengali, Tiro Bangla, Hind
Siliguri and Noto Sans Bengali have been verified (Hind joins conjuncts
with half forms, Noto Sans varies ে/ৈ by the letter; the converter detects
both). Include the `bengali` preset, and the UI
sizes if you want Bengali book titles to render in the library:

    pip install freetype-py fonttools uharfbuzz
    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      NotoSerifBengali-Regular.ttf \
      --intervals latin-ext,bengali \
      --sizes 8,10,12,14,16,18 \
      --name NotoSerifBengali \
      --output-dir ./NotoSerifBengali/

The converter also records the contextual forms of the vowel signs the font
selects on the desktop (word-initial ে/ৈ, word-final া/ী/ৗ, ি and ী matched to
wide conjuncts or fused with a reph), so the device output matches HarfBuzz
glyph for glyph on more than 99% of the words in a typical novel. The cluster
table is about 6-7 KB per size; one copy per family stays in RAM while any
size of the font is loaded.

Fonts built without shaping (or by older converters) still load: the firmware
reorders the vowel signs and positions the marks, but conjuncts then show as
consonant + hasanta + consonant. Bengali strings in the UI (book titles, file
names) are drawn with an installed Bengali family whether or not it is the
reader font (see [UI fallback by script](#ui-fallback-by-script)).

### Devanagari (Hindi, Marathi, Nepali, Sanskrit)

Devanagari uses the same pre-shaping approach with the differences of the
script: consonants before a virama take their half forms (स्त, क्ष्म), the
ligatures the font has (क्ष, त्र, द्ध, ट्ठ) and rakar (क्र) are composite
glyphs, the reph is drawn after the vowel sign that follows the syllable
(र्का, र्के), and the forms the font fuses with the reph or the anusvara
(र्कि, र्कं, कें, कों, नहीं) are recorded from HarfBuzz. Noto Serif
Devanagari and Noto Sans Devanagari have been verified (98% or better of the
words of a Premchand novel match HarfBuzz glyph for glyph; the remainder are
width variants of ि and ी before conjuncts assembled from half forms). Use the
`devanagari` preset:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      NotoSerifDevanagari-Regular.ttf \
      --intervals latin-ext,devanagari \
      --sizes 8,10,12,14,16,18 \
      --name NotoSerifDevanagari \
      --output-dir ./NotoSerifDevanagari/

The cluster table is 7-9 KB per size for the Noto families and 14 KB for Tiro
Devanagari Sanskrit; one copy per family stays in RAM.

Install custom fonts via the web interface or manual SD card copy.
