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
PAGES = ["FIRST", "WRITE", "SENT", "RECENT", "NEIGHBORS", "RADIO", "POWER", "SCAN", "FSCAN",
         "BLUETOOTH", "GPS", "BEACON", "TICKER", "SHUTDOWN"]

TARGETS = ["#public", "#Squad North", "@Alexey", "@Base Camp", "@Fox"]
# Layouts follow `set lang ru|en` (ru is the default).
LANG = {
    "en": {"phrases": ["ok", "ping", "yes", "no", "hello", "goodbye",
                       "on my way", "arrived", "waiting", "where?", "thanks", "soon", "call back", "SOS"],
           "rows": ["1234567890", "qwertyuiop", "asdfghjkl?", "zxcvbnm,.!"],
           "actions": [(0, "space"), (4, "del"), (7, "SEND")],
           "keyboard": "keyboard"},
    "ru": {"phrases": ["ок", "ping", "да", "нет", "привет", "пока",
                       "в пути", "на месте", "жду", "где ты?", "спасибо", "скоро", "перезвоню", "SOS"],
           "rows": ["1234567890.,", "йцукенгшщзхъ", "фывапролджэ!", "ячсмитьбюё?:"],
           "actions": [(0, "space"), (5, "del"), (8, "SEND")],
           "keyboard": "клавиатура"},
}


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


P1 = 6  # phrases on the first screen, next to the keyboard button


def scr_phrases(d, lang="en", target="#Squad North", sel=0):
    L = LANG[lang]
    second = sel > P1
    title(d, target, "2/2" if second else "1/2")
    w = d.width()
    half = w // 2
    ph = L["phrases"]
    if not second:
        cell(d, 2, 15, w - 4, L["keyboard"], sel == 0, True)
        for i, p in enumerate(ph[:P1]):
            cell(d, (i % 2) * half + 2, 15 + (i // 2 + 1) * 12, half - 4, p, sel == i + 1, True)
    else:
        for k, p in enumerate(ph[P1:]):
            cell(d, (k % 2) * half + 2, 15 + (k // 2) * 12, half - 4, p, sel == P1 + k + 1, True)


NEIGHBORS = [("Repeater South", 9.25), ("Hill-2", 4.75), ("A3F09C12", -3.25), ("Bridge", -8.75),
             ("Tower East", -10.5), ("Base Camp", -12.0), ("Fox", -14.25)]


def scr_neighbors(d, n=7, page=0):
    """Neighbors page: "Name [7] 1/2" in the header, 4 neighbours per page."""
    ro.home_header(d, "NEIGHBORS")
    w = d.width()
    per = (d.height() - 20) // 11
    pages = (n + per - 1) // per
    suffix = " [%d] %d/%d" % (n, page + 1, pages) if pages > 1 else " [%d]" % n
    room = w - 24 - 5 - 3 - d.getTextWidth(suffix)
    d.setColor(BLACK); d.fillRect(0, 0, w - 24 - 5 - 1, 12)
    d.setColor(WHITE); d.setTextSize(1)
    name = d.translate(ro.NODE_NAME, 32)
    d.drawTextEllipsized(0, 2, room, name)
    nw = d.getTextWidth(name)
    d.setCursor(min(nw, room), 2); d.print(suffix)
    rows = NEIGHBORS[page * per:min(n, (page + 1) * per)]
    ro.name_list(d, [(nm, "%+.0f" % snr) for nm, snr in rows])


def scr_keys(d, lang="en", target="#Squad North", text="", row=1, col=0, re=False):
    L = LANG[lang]
    rows, actions = L["rows"], L["actions"]
    kc = len(rows[0])
    w = d.width()
    count = str(len(text))
    if not text:
        title(d, ("RE " if re else "to ") + target, count)
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
            cur = max(a for a, (c, _) in enumerate(actions) if col >= c)
            for a, (c0, label) in enumerate(actions):
                c1 = actions[a + 1][0] if a + 1 < len(actions) else kc
                x0, x1 = c0 * w // kc, c1 * w // kc
                cell(d, x0, y, x1 - x0 - 1, label, row == r and cur == a)
            continue
        for c in range(kc):
            x0, x1 = c * w // kc, (c + 1) * w // kc
            cell(d, x0, y, x1 - x0, rows[r][c], row == r and col == c)


def scr_result(d, target="@Alexey", line="Delivered", retry=False):
    title(d, target)
    w = d.width()
    d.setColor(WHITE); d.setTextSize(1)
    d.drawTextCentered(w // 2, 26, line)
    if retry:
        d.drawTextCentered(w // 2, 42, "hold: send again")
    d.drawTextCentered(w // 2, 53, "press: back")


ENVELOPE = [0xFF, 0xC3, 0xA5, 0x99, 0x81, 0x81, 0xFF]


def badge(d, unread):
    """Envelope and unread count left of the battery icon."""
    s = str(unread)
    bw = 8 + 2 + d.getTextWidth(s) + 4
    right = d.width() - 24 - 5 - 3 - bw
    d.setColor(BLACK); d.fillRect(0, 0, d.width() - 24 - 5 - 1, 12)
    d.setColor(WHITE); d.setTextSize(1)
    d.drawTextEllipsized(0, 2, right, d.translate(ro.NODE_NAME, 32))
    d.drawXbm(right + 2, 3, ENVELOPE, 8, 7)
    d.setCursor(right + 12, 2); d.print(s)


def scr_home_badge(d, unread=3):
    ro.scr_home(d, msg=unread, unread=unread)
    badge(d, unread)


def scr_write_badge(d, unread=3):
    scr_write_page(d)
    badge(d, unread)


def scr_pick_recent(d, sel=0, recent=2):
    """Recent recipients first, a dotted line, then channels and favourites."""
    title(d, "Write to:")
    w = d.width()
    items = ["@Алексей", "#Squad North", "#public", "@Base Camp"]
    for r, it in enumerate(items):
        y = 14 + r * 12
        d.setColor(WHITE)
        if r == sel:
            d.fillRect(0, y - 1, w, 11); d.setColor(BLACK)
        d.drawTextEllipsized(2, y, w - 4, d.translate(it, 48))
        d.setColor(WHITE)
        if r == recent - 1:
            for x in range(2, w - 2, 4):
                d.fillRect(x, y + 10, 2, 1)


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
    ("47_write_channel", "Ушло в канал", lambda d: scr_result(d, "#Squad North", "Sent to channel")),
    ("48_write_delivered", "Личное: доставлено", scr_result),
]

# Screens that depend on `set lang`: name, caption, function(d, lang)
LANG_SCREENS = [
    ("42_write_phrases", "Клавиатура и готовые фразы", lambda d, l: scr_phrases(d, l)),
    ("43_write_phrase_sel", "Выбрана фраза", lambda d, l: scr_phrases(d, l, sel=5)),
    ("43b_write_phrases_2", "Фразы, экран 2", lambda d, l: scr_phrases(d, l, sel=7)),
    ("44_write_keys", "Клавиатура: пусто", lambda d, l: scr_keys(d, l)),
    ("45_write_typing", "Набор текста", lambda d, l: scr_keys(
        d, l, text={"en": "meet at 18", "ru": "у моста в 18"}[l], row=2, col=3)),
    ("46_write_send", "Кнопка SEND", lambda d, l: scr_keys(
        d, l, text={"en": "meet at 18 bridge", "ru": "у моста в 18:00"}[l], row=4,
        col={"en": 7, "ru": 8}[l])),
]

# answering from the device: name, caption, function(d)
REPLY_SCREENS = [
    ("70_home_unread", "Главная: 3 непрочитанных", scr_home_badge),
    ("71_message", "Входящее: удержание — ответить",
     lambda d: ro.scr_message(d, "Ты где? Мы у моста", unread=3, origin="(1) Алексей:")),
    ("72_reply_phrases", "Ответ: RE и адресат в заголовке", lambda d: scr_phrases(d, "ru", "RE @Алексей", sel=0)),
    ("72b_reply_keys", "Ответ с клавиатуры", lambda d: scr_keys(d, "ru", "@Алексей", re=True)),
    ("73_reply_delivered", "Доставлено", lambda d: scr_result(d, "RE @Алексей", "Delivered")),
    ("74_reply_noreply", "Нет ответа: удержание — ещё раз",
     lambda d: scr_result(d, "RE @Алексей", "No reply", retry=True)),
    ("75_pick_recent", "Последние адресаты сверху", scr_pick_recent),
]

def scr_sent_page(d, n=4, replies=2):
    ro.home_header(d, "SENT")
    w = d.width()
    d.setColor(WHITE); d.setTextSize(1)
    d.drawTextCentered(w // 2, 22, "Sent: %d" % n)
    d.drawTextCentered(w // 2, 34, "replies: %d" % replies)
    d.drawTextCentered(w // 2, 64 - 11, "open: " + ro.PRESS_LABEL)


def scr_sent(d, target, pos, state, age, text, re=None):
    w = d.width()
    d.setTextSize(1); d.setColor(WHITE)
    pw = d.getTextWidth(pos)
    d.setCursor(w - pw - 1, 0); d.print(pos)
    d.drawTextEllipsized(0, 0, w - pw - 4, d.translate(target, 48))
    d.drawRect(0, 11, w, 1)
    d.drawTextEllipsized(0, 14, w, "%s  %s ago" % (state, age))
    y = 25
    if re:
        d.drawTextEllipsized(0, y, w, d.translate("RE: " + re, 60))
        y += 11
    d.setCursor(0, y)
    d.printWordWrap(d.translate(text, 121), w)


SENT_SCREENS = [
    ("80_sent_page", "Страница Sent", scr_sent_page),
    ("81_sent_reply_dm", "Ответ в личку: на что и статус",
     lambda d: scr_sent(d, "@Алексей", "1/4", "Delivered", "2m", "в пути", re="Ты где? Мы у моста")),
    ("82_sent_reply_ch", "Ответ в канал",
     lambda d: scr_sent(d, "#Squad North", "2/4", "To channel", "15m", "жду у моста в 18:00",
                        re="Fox: кто где?")),
    ("83_sent_noreply", "Личное без ответа",
     lambda d: scr_sent(d, "@Base Camp", "3/4", "No reply", "1h", "перезвоню")),
]

NEIGHBOR_SCREENS = [
    ("60_neighbors_3", "3 соседа: всё на одном экране", lambda d: scr_neighbors(d, 3)),
    ("61_neighbors_p1", "7 соседей: экран 1 из 2", lambda d: scr_neighbors(d, 7, 0)),
    ("62_neighbors_p2", "7 соседей: экран 2 из 2", lambda d: scr_neighbors(d, 7, 1)),
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
    sheet(items, 4, "Write — сообщение с устройства (OLED 128×64, T114)").save(
        os.path.join(out, "gallery_write.png"))

    titles = {"ru": "Русский: set lang ru (по умолчанию)", "en": "English: set lang en"}
    for lang in ("ru", "en"):
        litems = []
        for fname, label, fn in LANG_SCREENS:
            d = Dev(); fn(d, lang)
            im = to_image(d)
            im.save(os.path.join(out, "write", "%s_%s.png" % (fname, lang)))
            litems.append((label, im))
        sheet(litems, 3, "Write — " + titles[lang]).save(
            os.path.join(out, "gallery_write_%s.png" % lang))

    nitems = []
    os.makedirs(os.path.join(out, "neighbors"), exist_ok=True)
    for fname, label, fn in NEIGHBOR_SCREENS:
        d = Dev(); fn(d)
        im = to_image(d)
        im.save(os.path.join(out, "neighbors", fname + ".png"))
        nitems.append((label, im))
    sheet(nitems, 3, "Neighbors — число соседей и экран в заголовке").save(
        os.path.join(out, "gallery_neighbors.png"))

    ritems = []
    os.makedirs(os.path.join(out, "reply"), exist_ok=True)
    for fname, label, fn in REPLY_SCREENS:
        d = Dev(); fn(d)
        im = to_image(d)
        im.save(os.path.join(out, "reply", fname + ".png"))
        ritems.append((label, im))
    sheet(ritems, 3, "Ответ с устройства и непрочитанные").save(
        os.path.join(out, "gallery_reply.png"))

    sitems = []
    os.makedirs(os.path.join(out, "sent"), exist_ok=True)
    for fname, label, fn in SENT_SCREENS:
        d = Dev(); fn(d)
        im = to_image(d)
        im.save(os.path.join(out, "sent", fname + ".png"))
        sitems.append((label, im))
    sheet(sitems, 2, "Sent — отправленные с устройства").save(os.path.join(out, "gallery_sent.png"))

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
