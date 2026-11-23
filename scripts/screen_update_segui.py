#!/usr/bin/env python3
"""
scripts/screen_update_segui.py

Turn the raw framebuffer dumps from `make segui-screens` (the headless
SegUI capture tool) into PNGs under img/segui/.

A raw frame is [u32 width][u32 height][width*height u32 pixels], where each
pixel is the host's in-memory COLOR word: 0xAARRGGBB stored little-endian, so
the byte order on disk is B, G, R, A.
"""

import glob
import os
import re
import struct
import sys
import zlib

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RAW_DIR = "/tmp/segui_raw_screens"
IMG_OUT_DIR = os.path.join(BASE_DIR, "img", "segui")
MOBILE_HTML = os.path.join(BASE_DIR, "devices/index.html")

MARKER = "<!-- \u00b5BTRON-SEGUI Toolkit UI Showcase -->"
TAIL_MARKER = "<!-- Relation to B-System -->"

# (file, tab/screen, badge, Japanese title, description)
SCREENS = [
    ("picocalc_launcher", "Home / Launcher", "320x320 3x3",
     "PicoCalc Palm OS Launcher",
     "Red Dot industrial retro 3x3 Palm OS 5 HiRes launcher grid with bespoke 12x12 pixel-art icons."),
    ("picocalc_terminal", "1. Terminal", "Shell",
     "B-System Terminal",
     "SEGUI_W_TEXT dark console ring mirrored to PL011 UART0."),
    ("picocalc_editor", "2. Editor", "TAD / Text",
     "B-System Editor",
     "Zero-heap scratchpad editor with cursor and RAM /SYS storage."),
    ("picocalc_browser", "3. Browser", "Hypertext",
     "TAD / Manual Browser",
     "2-Form local hypertext index and reader."),
    ("picocalc_cabinet", "4. Cabinet", "Volumes",
     "Real-Object Cabinet",
     "Browse /SYS RAM and SPI0 SDHC volumes."),
    ("picocalc_glgears", "5. GLGears", "3D Banded Z",
     "GLGears 3D Benchmark",
     "3-gear Lambertian software rasterizer using 10 KiB banded 16-bit Z-buffer."),
    ("picocalc_contacts", "6. Contacts", "Directory",
     "Contacts",
     "Palm OS Address Book directory opening directly into Chat."),
    ("picocalc_phone", "7. Phone", "Dialer",
     "Phone Dialer & Recents",
     "Dedicated Palm-style 3x4 numeric dialer keypad and call log."),
    ("picocalc_chat", "8. Chat", "LoRa Chat",
     "LoRa Mesh Chat",
     "Dedicated 2-Form LoRa Chat thread and Circles communicator."),
    ("picocalc_prefs", "9. Preferences", "Hardware",
     "PicoCalc Preferences",
     "Custom PicoCalc Preference window: LCD/Kbd backlight, Theme, Battery, SRAM, BOOTSEL, Power-Off."),
    ("picocalc_xmb", "XMB", "Opened",
     "XMB",
     "Pico XMB painter: cosine ribbon, category icons at the passive half size, the open column at full size on the selected row. Applications labels are the window titles. Host capture of xmb_pico_paint."),
    ("picocalc_quake", "Quake", "e1m1",
     "Quake e1m1",
     "The PicoCalc Quake app. start_sim renders the e1m1 cook through PicoGL into the 128x96 target and the shell blits it. The boot menu has DRONE highlighted."),
]


def raw_to_png(raw_path, png_path):
    with open(raw_path, "rb") as f:
        data = f.read()

    if len(data) < 8:
        print(f"[WARN] Raw file too small: {raw_path}")
        return False

    width, height = struct.unpack("<II", data[:8])
    expected = width * height * 4
    if len(data) - 8 < expected:
        print(f"[WARN] Truncated data for {raw_path} "
              f"(got {len(data) - 8}, expected {expected})")
        return False

    bgra = data[8:8 + expected]

    # BGRA in memory -> RGBA for the PNG scannerline, one stride per channel.
    rgba = bytearray(expected)
    rgba[0::4] = bgra[2::4]
    rgba[1::4] = bgra[1::4]
    rgba[2::4] = bgra[0::4]
    rgba[3::4] = bgra[3::4]

    row_bytes = width * 4
    # Each PNG scanline is prefixed with its filter type (0 = None).
    filtered = b"".join(
        b"\x00" + bytes(rgba[y * row_bytes:(y + 1) * row_bytes])
        for y in range(height)
    )

    def chunk(tag, payload):
        crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    png = b"".join([
        b"\x89PNG\r\n\x1a\n",
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)),
        chunk(b"IDAT", zlib.compress(filtered, 9)),
        chunk(b"IEND", b""),
    ])

    os.makedirs(os.path.dirname(png_path), exist_ok=True)
    with open(png_path, "wb") as f:
        f.write(png)

    print(f"  [PNG] {os.path.basename(png_path)} "
          f"({width}x{height} px, {os.path.getsize(png_path)} bytes)")
    return True


def convert_all(raw_dir=RAW_DIR, out_dir=IMG_OUT_DIR):
    raws = sorted(glob.glob(os.path.join(raw_dir, "*.raw")))
    if not raws:
        print(f"[ERROR] No raw SegUI captures in {raw_dir}.")
        print("        Build them with `make segui-screens`.")
        return 0

    os.makedirs(out_dir, exist_ok=True)
    return sum(
        raw_to_png(r, os.path.join(out_dir, os.path.splitext(os.path.basename(r))[0] + ".png"))
        for r in raws
    )


BAND_CHIPS = [
    ("STATUS", "22 px", "clock and battery"),
    ("TABBAR", "22 px", "omitted when the form has one tab"),
    ("BODY", "stretch", "the only stretchable band"),
    ("SOFTKEY", "26 px", "three captions"),
]


def _card(i, entry):
    fname, tab, badge, jp_title, desc = entry
    n, total = i + 1, len(SCREENS)
    return f"""
                <!-- {n}. {tab} -->
                <div class="segui-card">
                    <div class="segui-frame">
                        <img src="../img/segui/{fname}.png"
                             alt="{n}. {jp_title} ({tab})"
                             class="segui-img" width="320" height="320">
                    </div>
                    <div class="segui-card-info">
                        <div class="segui-card-title">
                            <span>{n}. {jp_title}</span>
                            <span class="segui-badge">{badge}</span>
                        </div>
                        <div class="segui-card-sub">{tab}</div>
                        <div class="segui-card-desc">{desc}</div>
                    </div>
                </div>"""


def generate_segui_screens_html():
    chips = "\n".join(
        f'                    <li><b>{name}</b><span>{size}</span><em>{note}</em></li>'
        for name, size, note in BAND_CHIPS
    )
    cards = "\n".join(_card(i, e) for i, e in enumerate(SCREENS))

    return f"""
        {MARKER}
        <section class="audit-card" id="segui-screens">
            <h3><span>POCKET · BAND · CALC</span></h3>
            <p>
                320×320 未満は一つの帯レイアウトで描く。POCKET は掌の通信機、BAND は 128×160 に収まる帯、CALC は 100×100 未満の鍵である。下の画面は掌の基準パネル、PicoCalc の 320×320 である。320×320 自体は要求一覧に無い。帯の値は <code class="mono">segui_profile_picocalc</code>。伸長できる帯は BODY だけ。
            </p>
            <p style="font-size:0.9em; opacity:0.85; margin-top:0.6rem;">
                Below 320×320, one band layout draws three shells. POCKET is the one-hand set, BAND is the strap that fits in 128×160, and CALC is the key panel under 100×100. The frames are the pocket reference, the PicoCalc 320×320 panel. 320×320 itself is not a requested mode. Band sizes are <code class="mono">segui_profile_picocalc</code>. Only BODY may stretch.
            </p>

            <style>
                .segui-band-list {{
                    display: flex;
                    flex-wrap: wrap;
                    gap: 0.6rem;
                    list-style: none;
                    margin: 1.4rem 0 0 0;
                    padding: 0;
                }}
                .segui-band-list li {{
                    display: flex;
                    align-items: baseline;
                    gap: 0.5rem;
                    background: #f1f5f9;
                    border: 1px solid #cbd5e1;
                    border-left: 4px solid #0057b7;
                    border-radius: 8px;
                    padding: 0.45rem 0.7rem;
                    font-size: 0.82rem;
                    color: #475569;
                }}
                .segui-band-list li b {{
                    color: #003366;
                    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
                    letter-spacing: 0.04em;
                }}
                .segui-band-list li span {{
                    color: #0284c7;
                    font-weight: 700;
                    font-variant-numeric: tabular-nums;
                }}
                .segui-gallery-grid {{
                    display: grid;
                    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
                    gap: 1.8rem;
                    margin-top: 1.8rem;
                }}
                .segui-card {{
                    background: #f8fafc;
                    border: 1px solid #cbd5e1;
                    border-radius: 16px;
                    padding: 1.2rem;
                    display: flex;
                    flex-direction: column;
                    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.04);
                    transition: transform 0.2s ease, box-shadow 0.2s ease;
                }}
                .segui-card:hover {{
                    transform: translateY(-3px);
                    box-shadow: 0 8px 24px rgba(0, 87, 183, 0.12);
                    border-color: #0057b7;
                }}
                .segui-frame {{
                    background: #000;
                    border: 4px solid #1e293b;
                    border-radius: 12px;
                    overflow: hidden;
                    box-shadow: inset 0 0 10px rgba(0,0,0,0.5), 0 4px 12px rgba(0,0,0,0.15);
                    display: flex;
                    justify-content: center;
                }}
                .segui-img {{
                    width: 100%;
                    max-width: 320px;
                    height: auto;
                    display: block;
                    image-rendering: pixelated;
                }}
                .segui-card-info {{
                    width: 100%;
                    margin-top: 1rem;
                    text-align: left;
                }}
                .segui-card-title {{
                    font-size: 1.05rem;
                    font-weight: 700;
                    color: #003366;
                    margin-bottom: 0.2rem;
                    display: flex;
                    align-items: center;
                    justify-content: space-between;
                    gap: 0.6rem;
                }}
                .segui-card-sub {{
                    font-size: 0.78rem;
                    color: #64748b;
                    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
                    margin-bottom: 0.45rem;
                }}
                .segui-badge {{
                    font-size: 0.75rem;
                    background: #e0f2fe;
                    color: #0284c7;
                    border: 1px solid #bae6fd;
                    padding: 0.15rem 0.5rem;
                    border-radius: 999px;
                    font-weight: 600;
                    white-space: nowrap;
                }}
                .segui-card-desc {{
                    font-size: 0.88rem;
                    color: #475569;
                    line-height: 1.6;
                }}
            </style>

            <ul class="segui-band-list">
{chips}
            </ul>

            <div class="segui-gallery-grid">
{cards}

            </div>
        </section>
"""


def update_mobile_html():
    if not os.path.exists(MOBILE_HTML):
        print(f"[ERROR] devices/index.html not found at {MOBILE_HTML}")
        return False

    with open(MOBILE_HTML, "r", encoding="utf-8") as f:
        content = f.read()

    new_section = generate_segui_screens_html().strip()

    # Replace only this showcase. The FOMA injector stops at MARKER.
    # Anything between this section and TAIL_MARKER, including the FOMA
    # legacy card, stays where the page put it.
    pattern = re.compile(
        re.escape(MARKER) + r".*?</section>", flags=re.DOTALL
    )
    if pattern.search(content):
        updated = pattern.sub(
            lambda m: new_section, content, count=1
        )
    elif TAIL_MARKER in content:
        updated = content.replace(
            TAIL_MARKER, new_section + "\n\n        " + TAIL_MARKER, 1
        )
    elif "</main>" in content:
        updated = content.replace("</main>", new_section + "\n    </main>", 1)
    else:
        print(f"[ERROR] No insertion anchor found in {MOBILE_HTML}")
        return False

    with open(MOBILE_HTML, "w", encoding="utf-8") as f:
        f.write(updated)

    print(f"[OK] {MOBILE_HTML}: {len(SCREENS)} SegUI screens injected.")
    return True


def main():
    print("===============================================================")
    print(" SegUI Raw Frames -> PNG (img/segui) + devices/index.html")
    print("===============================================================")
    n = convert_all(*(sys.argv[1:3] if len(sys.argv) > 1 else ()))
    print(f" Total PNGs written: {n}")
    ok = update_mobile_html()
    print("===============================================================")
    return 0 if n and ok else 1


if __name__ == "__main__":
    sys.exit(main())
