"""Screens of the Write page and the running line (Msg scroll).

Reuses the OLED emulator from render_oled.py and redraws the same layout as
examples/companion_radio/ui-new/ComposeScreen.h and the ticker in UITask.cpp.

    python3 tools/screen_render/render_compose.py

Writes docs/screens/write/*.png, docs/screens/ticker/*.png and *.gif.
"""
import os
import re
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_oled as ro  # noqa: E402

from render_oled import Oled, BLACK, WHITE, ROOT, to_image, sheet  # noqa: E402

ro.NODE_NAME = "MeshNode"

# Home pages of a T114 / Heltec V3 / V4 companion with this fork.
PAGES = ["FIRST", "WRITE", "RECENT", "NEIGHBORS", "RADIO", "POWER", "SCAN", "FSCAN",
         "BLUETOOTH", "GPS", "BEACON", "TICKER", "SHUTDOWN"]

TARGETS = ["#public", "#Squad North", "@Alexey", "@Base Camp", "@Fox"]
PHRASES = ["ok", "ping", "yes", "no", "hello", "goodbye"]
KEY_ROWS = ["1234567890", "qwertyuiop", "asdfghjkl?", "zxcvbnm,.!"]
ACTIONS = [(0, "space"), (4, "del"), (7, "SEND")]


class Dev(Oled):
    pages = PAGES


# ------------------------------------------------------------ ComposeScreen

def title(d, left, right=None):
    w = d.width()
    d.setTextSize(1); d.setColor(WHITE)
    rw = d.getTextWidth(right) if right else 0
    if right:
        d.setCursor(w - rw - 1, 0); d.print(right)
    d.drawTextEllipsized(0, 0, w - rw - 4, d.translate(left, 48))
    d.drawRect(0, 11, w, 1)


def cell(d, x, y, w, text, sel, framed=False):
    top = y - 2 if framed else y - 1
    h = 11 if framed else 10
    d.setColor(WHITE)
    if sel:
        d.fillRect(x, top, w, h); d.setColor(BLACK)
    elif framed:
        d.drawRect(x, top, w, h)
    tw = d.getTextWidth(text)
    if tw > w - 2:
        d.drawTextEllipsized(x + 1, y, w - 2, text)
    else:
        d.setCursor(x + (w - tw) // 2, y); d.print(text)
    d.setColor(WHITE)


def scr_write_page(d):
    ro.home_header(d, "WRITE")
    w = d.width()
    d.setColor(WHITE); d.setTextSize(1)
    d.drawTextCentered(w // 2, 22, "Write message")
    d.drawTextCentered(w // 2, 34, "channel or contact")
    d.drawTextCentered(w // 2, 64 - 11, "open: " + ro.PRESS_LABEL)


def scr_pick(d, sel=1):
    title(d, "Write to:")
    w = d.width()
    for r in range(4):
        y = 14 + r * 12
        d.setColor(WHITE)
        if r == sel:
            d.fillRect(0, y - 1, w, 11); d.setColor(BLACK)
        d.drawTextEllipsized(2, y, w - 4, TARGETS[r])
        d.setColor(WHITE)


def scr_phrases(d, target="#Squad North", sel=0):
    title(d, target)
    w = d.width()
    half = w // 2
    cell(d, 2, 15, w - 4, "keyboard", sel == 0, True)
    for i, p in enumerate(PHRASES):
        cell(d, (i % 2) * half + 2, 15 + (i // 2 + 1) * 12, half - 4, p, sel == i + 1, True)


def scr_keys(d, target="#Squad North", text="", row=1, col=0):
    w = d.width()
    count = str(len(text))
    if not text:
        title(d, "to " + target, count)
    else:
        room = w - d.getTextWidth(count) - 6
        frm = 0
        while d.getTextWidth(text[frm:] + "_") > room and frm < len(text):
            frm += 1
        d.setTextSize(1); d.setColor(WHITE)
        d.setCursor(w - d.getTextWidth(count) - 1, 0); d.print(count)
        d.setCursor(0, 0); d.print(text[frm:] + "_")
        d.drawRect(0, 11, w, 1)
    for r in range(5):
        y = 13 + r * 10
        if r == 4:
            cur = max(a for a, (c, _) in enumerate(ACTIONS) if col >= c)
            for a, (c0, label) in enumerate(ACTIONS):
                c1 = ACTIONS[a + 1][0] if a + 1 < len(ACTIONS) else 10
                x0, x1 = c0 * w // 10, c1 * w // 10
                cell(d, x0, y, x1 - x0 - 1, label, row == r and cur == a)
            continue
        for c in range(10):
            x0, x1 = c * w // 10, (c + 1) * w // 10
            cell(d, x0, y, x1 - x0, KEY_ROWS[r][c], row == r and col == c)


def scr_result(d, target="@Alexey", line="Delivered"):
    title(d, target)
    w = d.width()
    d.setColor(WHITE); d.setTextSize(1)
    d.drawTextCentered(w // 2, 26, line)
    d.drawTextCentered(w // 2, 53, "press: back")


def scr_ticker_page(d, on=True):
    ro.home_header(d, "TICKER")
    w = d.width()
    d.setColor(WHITE); d.setTextSize(1)
    d.drawTextCentered(w // 2, 18, "Msg scroll")
    d.setTextSize(2)
    d.drawTextCentered(w // 2, 31, "ON" if on else "OFF")
    d.setTextSize(1)
    d.drawTextCentered(w // 2, 64 - 11, "toggle: " + ro.PRESS_LABEL)


WRITE_SCREENS = [
    ("40_write_page", "Страница Write", scr_write_page),
    ("41_write_pick", "Кому: канал или избранный", scr_pick),
    ("42_write_phrases", "Клавиатура и готовые фразы", scr_phrases),
    ("43_write_phrase_sel", "Фраза «hello»", lambda d: scr_phrases(d, sel=5)),
    ("44_write_keys", "Клавиатура: пусто", scr_keys),
    ("45_write_typing", "Набор текста", lambda d: scr_keys(d, text="meet at 18", row=2, col=3)),
    ("46_write_send", "Кнопка SEND", lambda d: scr_keys(d, text="meet at 18 bridge", row=4, col=7)),
    ("47_write_channel", "Ушло в канал", lambda d: scr_result(d, "#Squad North", "Sent to channel")),
    ("48_write_delivered", "Личное: доставлено", scr_result),
]

TICKER_SCREENS = [
    ("50_ticker_on", "Msg scroll: ON", scr_ticker_page),
    ("51_ticker_off", "Msg scroll: OFF", lambda d: scr_ticker_page(d, False)),
]

# ------------------------------------------------------------ running line

def load_ticker_font():
    path = os.path.join(ROOT, "src", "helpers", "ui", "TickerFont.h")
    with open(path, encoding="utf-8") as f:
        text = f.read()
    glyphs = {}
    for m in re.finditer(r"\{\s*0x([0-9A-Fa-f]+),\s*(\d+),\s*\{([^}]*)\}\s*\}", text):
        cols = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", m.group(3))]
        glyphs[int(m.group(1), 16)] = cols[:int(m.group(2))]
    return glyphs


TICKER_GLYPHS = load_ticker_font()


def ticker_cols(msg):
    out, gap = [], True
    for ch in msg:
        cp = ord(ch)
        if ord("a") <= cp <= ord("z"): cp -= 0x20
        elif 0x430 <= cp <= 0x44F: cp -= 0x20
        elif cp == 0x451: cp = 0x401
        g = TICKER_GLYPHS.get(cp)
        if g is None:
            if not gap: out += [0, 0, 0]; gap = True
            continue
        out += g + [0]
        gap = False
    return out


class Frame:
    """Physical framebuffer: logical 128x64 UI scaled up, plus dots drawn in pixels."""

    def __init__(self, fw, fh):
        self.fw, self.fh = fw, fh
        self.fb = [[0] * fw for _ in range(fh)]

    def paste_logical(self, d):
        for y in range(self.fh):
            ly = y * 64 // self.fh
            for x in range(self.fw):
                self.fb[y][x] = d.fb[ly][x * 128 // self.fw]

    def fill(self, x, y, w, h):
        for yy in range(max(0, y), min(self.fh, y + h)):
            for xx in range(max(0, x), min(self.fw, x + w)):
                self.fb[yy][xx] = 1


def ticker_frame(fw, fh, origin, cols, pos, dot_max=8, dot_gap=2):
    d = Dev()
    d.setTextSize(1); d.setColor(WHITE)
    d.drawTextEllipsized(0, 0, d.width(), d.translate(origin, 62))
    d.drawRect(0, 11, d.width(), 1)
    f = Frame(fw, fh)
    f.paste_logical(d)
    top = (13 * fh + 63) // 64
    dot = dot_max
    if dot * 7 > fh - top: dot = (fh - top) // 7
    dot = max(dot, 2)
    lit = dot - dot_gap if dot > dot_gap + 1 else dot
    screen_cols = (fw + dot - 1) // dot
    x0 = (fw - screen_cols * dot) // 2
    y0 = top + (fh - top - 7 * dot) // 2
    for c in range(screen_cols):
        src = pos - screen_cols + c
        if src < 0 or src >= len(cols): continue
        bits, r = cols[src], 0
        while bits:
            if bits & 1: f.fill(x0 + c * dot, y0 + r * dot, lit, lit)
            bits >>= 1; r += 1
    return f, screen_cols


def frame_image(f, scale, pad):
    on, off, bg = ro.ON, ro.OFF, ro.BG
    img = Image.new("RGB", (f.fw * scale + pad * 2, f.fh * scale + pad * 2), bg)
    px = img.load()
    for y in range(f.fh):
        for x in range(f.fw):
            c = on if f.fb[y][x] else off
            for j in range(scale - 1 if scale > 2 else scale):
                for i in range(scale - 1 if scale > 2 else scale):
                    px[pad + x * scale + i, pad + y * scale + j] = c
    return img


TICKER_ORIGIN = "(1) Алексей:"
TICKER_MSG = "Встречаемся у моста в 18:00"


def ticker_gif(path, fw, fh, scale, every=2):
    cols = ticker_cols(TICKER_MSG)
    _, screen_cols = ticker_frame(fw, fh, TICKER_ORIGIN, cols, 0)
    frames = []
    for pos in range(0, len(cols) + screen_cols + 1, every):
        f, _ = ticker_frame(fw, fh, TICKER_ORIGIN, cols, pos)
        frames.append(frame_image(f, scale, 3 * scale).convert("P", palette=Image.ADAPTIVE, colors=8))
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=50 * every, loop=0,
                   optimize=True, disposal=1)
    return frames


def main():
    out = os.path.join(ROOT, "docs", "screens")
    os.makedirs(os.path.join(out, "write"), exist_ok=True)
    os.makedirs(os.path.join(out, "ticker"), exist_ok=True)

    items = []
    for fname, label, fn in WRITE_SCREENS:
        d = Dev(); fn(d)
        im = to_image(d)
        im.save(os.path.join(out, "write", fname + ".png"))
        items.append((label, im))
    sheet(items, 3, "Write — сообщение с устройства (OLED 128×64, T114)").save(
        os.path.join(out, "gallery_write.png"))

    titems = []
    for fname, label, fn in TICKER_SCREENS:
        d = Dev(); fn(d)
        im = to_image(d)
        im.save(os.path.join(out, "ticker", fname + ".png"))
        titems.append((label, im))

    # still frames of the running line on a 128x64 OLED
    cols = ticker_cols(TICKER_MSG)
    _, sc = ticker_frame(128, 64, TICKER_ORIGIN, cols, 0)
    for n, pos in enumerate((sc, sc + 40)):
        f, _ = ticker_frame(128, 64, TICKER_ORIGIN, cols, pos)
        im = frame_image(f, ro.SCALE, 3 * ro.SCALE)
        im.save(os.path.join(out, "ticker", "52_ticker_frame_%d.png" % (n + 1)))
        titems.append(("Бегущая строка, кадр %d" % (n + 1), im))
    sheet(titems, 2, "Msg scroll — бегущая строка").save(os.path.join(out, "gallery_ticker.png"))

    ticker_gif(os.path.join(out, "ticker", "ticker_oled.gif"), 128, 64, 4)
    ticker_gif(os.path.join(out, "ticker", "ticker_t114.gif"), 240, 135, 2)
    print("saved to", out)


if __name__ == "__main__":
    main()
