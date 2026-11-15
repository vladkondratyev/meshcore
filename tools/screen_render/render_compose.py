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
PAGES = ["FIRST", "WRITE", "SENT", "RELAYS", "RECENT", "NEIGHBORS", "RADIO", "POWER", "SCAN", "FSCAN",
         "BLUETOOTH", "GPS", "BEACON", "TICKER", "SHUTDOWN"]

TARGETS = ["#public", "#Squad North", "@Alexey", "@Base Camp", "@Fox"]
# Layouts follow `set lang ru|en` (ru is the default).
LANG = {
    "en": {"phrases": ["ok", "ping", "yes", "no", "hello", "goodbye",
                       "on my way", "arrived", "waiting", "where?", "thanks", "soon", "call back", "SOS",
                       "weather today", "weather tomorrow", "rate", "moon"],
           "rows": ["qwertyuiop", "asdfghjkl.", "zxcvbnm,?!"],
           "freq": ["itsup?qkyc", "aeolg.!vfd", "hnrw,xzjbm"],
           "sym": ["1234567890", "-+=/()@#%&", "!?:;\"'*_~$"],
           "actions": [(0, "space"), (3, "#+"), (5, "del"), (7, "SEND")], "abc": "abc",
           "keyboard": "keyboard"},
    "ru": {"phrases": ["ок", "ping", "да", "нет", "привет", "пока",
                       "в пути", "на месте", "жду", "где ты?", "спасибо", "скоро", "перезвоню", "SOS",
                       "погода сегодня", "погода завтра", "Курс", "луна"],
           "rows": ["йцукенгшщзхъ", "фывапролджэ.", "ячсмитьбюё,?"],
           "freq": ["таилячшфэжзп", "ноеруб.щц,гм", "ксвдьх?ъёюйы"],
           "sym": ["1234567890.,", "-+=/()@#%&*_", "!?:;\"'~$<>[]"],
           "actions": [(0, "space"), (4, "#+"), (6, "del"), (9, "SEND")], "abc": "абв",
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


def layout_phrases(d, ph):
    """Same as ComposeScreen::layoutPhrases: a phrase wider than half a row takes the whole row."""
    half = d.width() // 2
    page, row, col, slots = 0, 1, 0, []
    for p in ph:
        wide = d.getTextWidth(d.translate(p, 36)) > half - 6
        if wide and col == 1:
            row, col = row + 1, 0
        if row > 3:
            page, row, col = page + 1, 0, 0
        slots.append((page, row, col, wide))
        if wide or col == 1:
            row, col = row + 1, 0
        else:
            col = 1
    return slots, page + 1


def scr_phrases(d, lang="en", target="#Squad North", sel=0, gps=False, smart=()):
    L = LANG[lang]
    ph = [">" + x for x in smart] + L["phrases"]
    slots, pages = layout_phrases(d, ph)
    fp = 2 if gps else 1
    page = 0 if sel < fp else slots[sel - fp][0]
    title(d, target, "%d/%d" % (page + 1, pages))
    w = d.width()
    half = w // 2
    if page == 0:
        if gps:
            cell(d, 2, 15, half - 4, "клавиши" if lang == "ru" else "keys", sel == 0, True)
            cell(d, half + 2, 15, half - 4, "моё место" if lang == "ru" else "location", sel == 1, True)
        else:
            cell(d, 2, 15, w - 4, L["keyboard"], sel == 0, True)
    for i, p in enumerate(ph):
        pg, row, col, wide = slots[i]
        if pg != page:
            continue
        y = 15 + row * 12
        if wide:
            cell(d, 2, y, w - 4, p, sel == i + fp, True)
        else:
            cell(d, col * half + 2, y, half - 4, p, sel == i + fp, True)


def scr_msg_settings(d, edit=False, row=0, scroll=True, freq=False, caps=True, lang="ru", cursor=False):
    ro.home_header(d, "TICKER")
    w = d.width()
    lay = ("частотн." if lang == "ru" else "freq") if freq else ("ЙЦУКЕН" if lang == "ru" else "QWERTY")
    rows = [("Msg scroll", "ON" if scroll else "OFF"), ("Keys layout", lay),
            ("Auto caps", "ON" if caps else "OFF"), ("Auto cursor", "ON" if cursor else "OFF")]
    n = len(rows)
    top = row - 2 if edit and row >= 3 else 0
    d.setTextSize(1)
    for i in range(top, top + 3):
        l, r = rows[i]
        y = 20 + (i - top) * 11
        d.setColor(WHITE)
        if edit and i == row:
            d.fillRect(0, y - 1, w, 10); d.setColor(BLACK)
        d.setCursor(2, y); d.print(l)
        rr = d.translate(r, 16)
        d.setCursor(w - 3 - d.getTextWidth(rr), y); d.print(rr)
    d.setColor(WHITE)
    d.fillRect(w - 1, 19 + 32 * top // n, 1, 32 * 3 // n)
    d.drawTextCentered(w // 2, 64 - 11, "hold:change 3x:done" if edit else "settings: " + ro.PRESS_LABEL)


def scr_relays_page(d, n=3, on=1):
    ro.home_header(d, "RELAYS")
    w = d.width()
    d.setColor(WHITE); d.setTextSize(1)
    d.drawTextCentered(w // 2, 22, "Relays: %d" % n)
    d.drawTextCentered(w // 2, 34, "on: %d" % on if n else "none set up")
    d.drawTextCentered(w // 2, 64 - 11, "open: " + ro.PRESS_LABEL)


def scr_relays(d, lines, sel=0, count=3):
    """lines: (left, right) pairs as RelayScreen draws them."""
    w = d.width()
    d.setTextSize(1); d.setColor(WHITE)
    d.drawTextEllipsized(0, 0, w, d.translate("Реле: %d" % count, 30))
    d.drawRect(0, 11, w, 1)
    top = max(0, sel - 3)
    for r, (left, right) in enumerate(lines[top:top + 4]):
        i = top + r
        y = 14 + r * 12
        d.setColor(WHITE)
        if i == sel:
            d.fillRect(0, y - 1, w, 11); d.setColor(BLACK)
        rs = d.translate(right, 12)
        rw = d.getTextWidth(rs)
        d.drawTextEllipsized(2, y, w - rw - 8, d.translate(left, 40))
        if right:
            d.setCursor(w - rw - 2, y); d.print(rs)
        d.setColor(WHITE)


RELAY_LIST = [("1 насос", "ВКЛ"), ("2 свет", "ВЫКЛ"), ("3 ворота", "?"), ("Найти реле", "")]

RELAY_SCREENS = [
    ("c1_relays_page", "Страница Relays в меню", lambda d: scr_relays_page(d)),
    ("c2_relays_list", "Удержание — вкл/выкл", lambda d: scr_relays(d, RELAY_LIST, 0)),
    ("c3_relays_busy", "Переключается…", lambda d: scr_relays(d, [("1 насос", "..."), ("2 свет", "ВЫКЛ"),
                                                                  ("3 ворота", "?"), ("Найти реле", "")], 0)),
    ("c4_relays_scan", "Поиск Bluetooth-реле рядом", lambda d: scr_relays(d, RELAY_LIST[:3] + [("Поиск...", "")], 3)),
    ("c5_relays_found", "Найдено: удержание — добавить",
     lambda d: scr_relays(d, RELAY_LIST + [("+ JDY-16", "-61"), ("+ LCTECH-4", "-74")], 4)),
    ("c6_relays_mesh", "Написали «реле насос вкл» — ответ",
     lambda d: ro.scr_message(d, "реле 1 насос: ВКЛ", unread=1, origin="(1) Дача:")),
]

SETTINGS_SCREENS = [
    ("90_msg_settings", "Настройки: удержание — открыть", lambda d: scr_msg_settings(d)),
    ("91_msg_settings_kb", "Раскладка и заглавные", lambda d: scr_msg_settings(d, True, 1)),
    ("92_msg_settings_cursor", "Автокурсор", lambda d: scr_msg_settings(d, True, 3)),
]

GPS_SCREENS = [
    ("b1_gps_key", "GPS включён: «моё место» рядом с клавишами",
     lambda d: scr_phrases(d, "ru", "#Squad North", sel=1, gps=True)),
    ("b2_gps_sent", "В Sent: geo:широта,долгота",
     lambda d: scr_sent(d, "#Squad North", "1/3", "To channel", "0s", "geo:55.75210,37.61750")),
    ("b3_gps_key_en", "set lang en: location",
     lambda d: scr_phrases(d, "en", "@Alexey", sel=1, gps=True)),
]

SMART_SCREENS = [
    ("a1_smart_where", "«Ты где?» — ответы сверху",
     lambda d: scr_phrases(d, "ru", "RE @Алексей", sel=2, smart=("в пути", "на месте", "дома"))),
    ("a2_smart_when", "«Когда будешь?»",
     lambda d: scr_phrases(d, "ru", "RE @Fox", sel=2, smart=("скоро", "через 10 мин", "через час"))),
    ("a3_next_word", "После «Буду » — следующее слово",
     lambda d: scr_keys(d, "ru", "@Алексей", text="Буду ", row=3, col=0)),
    ("a4_next_word2", "После «через » — дальше",
     lambda d: scr_keys(d, "ru", "@Алексей", text="Буду через ", row=3, col=8)),
    ("a5_settings_cursor", "Auto cursor в настройках",
     lambda d: scr_msg_settings(d, True, 3, cursor=True)),
    ("a6_save_phrase", "Из Sent: сделать фразой", lambda d: scr_save_phrase(d)),
]

JOURNEY_SCREENS = [
    ("01_home_unread", "1. Пришло сообщение: конверт в заголовке", lambda d: scr_home_badge(d, 1, connected=False)),
    ("02_message", "2. Читаете. Удержание — ответить",
     lambda d: ro.scr_message(d, "Ты где? Мы у моста", unread=1, origin="(1) Алексей:")),
    ("03_reply_phrases", "3. RE: фраза одной кнопкой или свой текст",
     lambda d: scr_phrases(d, "ru", "RE @Алексей", sel=1, smart=("в пути", "на месте", "дома"))),
    ("04_reply_keys", "4. Клавиатура с подсказками слов",
     lambda d: scr_keys(d, "ru", "@Алексей", text="Буду у мо", row=3, col=8)),
    ("05_delivered", "5. Доставлено", lambda d: scr_result(d, "RE @Алексей", "Delivered")),
    ("06_sent", "6. История: кому, на что, статус",
     lambda d: scr_sent(d, "@Алексей", "1/4", "Delivered", "1m", "Буду у моста", re="Ты где? Мы у моста")),
]

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


_WH = open(os.path.join(ROOT, "examples", "companion_radio", "ui-new", "WordHints.h"), encoding="utf-8").read()
WORDS = {l: re.findall(r'"([^"]+)"', _WH.split("WORDS_%s[] = {" % l.upper())[1].split("};")[0])
         for l in ("ru", "en")}


_SR = open(os.path.join(ROOT, "examples", "companion_radio", "ui-new", "SmartReplies.h"), encoding="utf-8").read()
NEXT = {l: dict(re.findall(r'\{ "([^"]+)", "([^"]+)" \}', _SR.split("NEXT_%s[] = {" % l.upper())[1].split("};")[0]))
        for l in ("ru", "en")}


def hints_for(lang, text):
    """Same as ComposeScreen::updateHints with an empty Sent list."""
    part = text.split(" ")[-1]
    if not part:
        prev = text.rstrip(" ").split(" ")[-1].lower() if text.strip() else ""
        nxt = NEXT[lang].get(prev, "").split("|") if prev and prev[-1] not in ".,!?:" else []
        return (nxt + ["", ""])[:2]
    out = []
    for w in WORDS[lang]:
        if len(w) > len(part) and w.lower().replace("ё", "е").startswith(part.lower().replace("ё", "е")) \
                and w.lower() not in [o.lower() for o in out]:
            out.append(w)
        if len(out) == 2:
            break
    return (out + ["", ""])[:2]


def caps_due(text):
    if not text:
        return True
    if not text.endswith(" "):
        return False
    t = text.rstrip(" ")
    return not t or t[-1] in ".!?"


def scr_keys(d, lang="en", target="#Squad North", text="", row=1, col=0, re=False,
             sym=False, freq=False, legend=False, caps=True):
    L = LANG[lang]
    rows = L["sym"] if sym else (L["freq"] if freq else L["rows"])
    actions = [(c, (L["abc"] if sym and lab == "#+" else lab)) for c, lab in L["actions"]]
    kc = len(rows[0])
    w = d.width()
    per = 2 if lang == "ru" else 1
    used = len(text.encode("utf-8"))
    count = "%d/%d" % (len(text), len(text) + (120 - used) // per)
    if legend:
        title(d, "1> 2< 3v 4^ 5:выход" if lang == "ru" else "1> 2< 3v 4^ 5:exit")
    elif not text:
        title(d, ("RE " if re else "to ") + target, count)
    else:
        room = w - d.getTextWidth(count) - 6
        frm = 0
        while d.getTextWidth(d.translate(text[frm:] + "_", 130)) > room and frm < len(text):
            frm += 1
        d.setTextSize(1); d.setColor(WHITE)
        d.setCursor(w - d.getTextWidth(count) - 1, 0); d.print(count)
        d.setCursor(0, 0); d.print(d.translate(text[frm:] + "_", 130))
        d.drawRect(0, 11, w, 1)
    up = caps and not sym and caps_due(text)
    hints = hints_for(lang, text) if not sym else ["", ""]
    for r in range(5):
        y = 13 + r * 10
        if r == 4:
            cur = max(a for a, (c, _) in enumerate(actions) if col >= c)
            for a, (c0, label) in enumerate(actions):
                c1 = actions[a + 1][0] if a + 1 < len(actions) else kc
                x0, x1 = c0 * w // kc, c1 * w // kc
                cell(d, x0, y, x1 - x0 - 1, d.translate(label, 16), row == r and cur == a)
            continue
        if r == 3:
            cur = 0 if col < kc // 2 else 1
            for k in range(2):
                x0, x1 = k * w // 2, (k + 1) * w // 2
                cell(d, x0, y, x1 - x0 - 1, d.translate(hints[k] or "-", 40), row == r and cur == k)
            continue
        for c in range(kc):
            x0, x1 = c * w // kc, (c + 1) * w // kc
            ch = rows[r][c]
            if up:
                ch = ch.upper()
            cell(d, x0, y, x1 - x0, d.translate(ch, 8), row == r and col == c)
            if c == col and r != row and row < 3:
                d.fillRect(x0 + 1, y + 8, x1 - x0 - 2, 1)
        if r == row:
            d.drawRect(0, y - 1, w, 10)


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


def scr_home_badge(d, unread=3, connected=True):
    ro.scr_home(d, connected=connected, msg=unread, unread=unread)
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
    ("43c_write_phrases_3", "Фразы, экран 3", lambda d, l: scr_phrases(d, l, sel=15)),
    ("44_write_keys", "Открыта: подсказка кнопок", lambda d, l: scr_keys(d, l, legend=True)),
    ("44b_write_caps", "Пусто: первая буква заглавная", lambda d, l: scr_keys(d, l)),
    ("45_write_typing", "Набор: подсказки слов", lambda d, l: scr_keys(
        d, l, text={"en": "Meet at the bri", "ru": "Буду у мо"}[l], row=1, col=3)),
    ("45b_write_hint", "Удержание на слове — вставить", lambda d, l: scr_keys(
        d, l, text={"en": "Meet at the bri", "ru": "Буду у мо"}[l], row=3, col=8)),
    ("46_write_sym", "#+: цифры и знаки", lambda d, l: scr_keys(
        d, l, text={"en": "Meet at the bridge ", "ru": "Буду у моста в "}[l], row=0, col=1, sym=True)),
    ("46b_write_send", "Кнопка SEND", lambda d, l: scr_keys(
        d, l, text={"en": "Meet at the bridge 18:00", "ru": "Буду у моста в 18:00"}[l], row=4,
        col={"en": 7, "ru": 9}[l])),
    ("47_write_freq", "Частотная раскладка (опция)", lambda d, l: scr_keys(
        d, l, text={"en": "Meet at the bri", "ru": "Буду у мо"}[l], row=1, col=1, freq=True)),
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


def scr_save_phrase(d, slot=18, now="луна", text="Буду у моста"):
    w = d.width()
    d.setTextSize(1); d.setColor(WHITE)
    d.drawTextEllipsized(0, 0, w, d.translate("Сделать фразой", 30))
    d.drawRect(0, 11, w, 1)
    d.drawTextEllipsized(0, 14, w, d.translate("фраза %d: %s" % (slot, now), 60))
    d.drawTextEllipsized(0, 26, w, d.translate("станет: " + text, 60))
    d.drawTextCentered(w // 2, 52, d.translate("1:номер держ:да 3:нет", 40))


SENT_SCREENS = [
    ("80_sent_page", "Страница Sent", scr_sent_page),
    ("81_sent_reply_dm", "Ответ в личку: на что и статус",
     lambda d: scr_sent(d, "@Алексей", "1/4", "Delivered", "2m", "в пути", re="Ты где? Мы у моста")),
    ("82_sent_reply_ch", "Ответ в канал",
     lambda d: scr_sent(d, "#Squad North", "2/4", "To channel", "15m", "жду у моста в 18:00",
                        re="Fox: кто где?")),
    ("83_sent_noreply", "Личное без ответа",
     lambda d: scr_sent(d, "@Base Camp", "3/4", "No reply", "1h", "перезвоню")),
    ("84_sent_save", "4 нажатия: сделать фразой", scr_save_phrase),
]

NEIGHBOR_SCREENS = [
    ("60_neighbors_3", "3 соседа: всё на одном экране", lambda d: scr_neighbors(d, 3)),
    ("61_neighbors_p1", "7 соседей: экран 1 из 2", lambda d: scr_neighbors(d, 7, 0)),
    ("62_neighbors_p2", "7 соседей: экран 2 из 2", lambda d: scr_neighbors(d, 7, 1)),
]

TICKER_SCREENS = [
    ("50_ticker_on", "Msg scroll: ON", lambda d: scr_msg_settings(d)),
    ("51_ticker_off", "Msg scroll: OFF", lambda d: scr_msg_settings(d, True, 0, scroll=False)),
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

    aitems = []
    os.makedirs(os.path.join(out, "smart"), exist_ok=True)
    for fname, label, fn in SMART_SCREENS:
        d = Dev(); fn(d)
        im = to_image(d)
        im.save(os.path.join(out, "smart", fname + ".png"))
        aitems.append((label, im))
    sheet(aitems, 3, "Меньше набора: готовые ответы, следующее слово, свои фразы").save(
        os.path.join(out, "gallery_smart.png"))

    # one message from start to end, without a phone
    jitems = []
    os.makedirs(os.path.join(out, "autonomy"), exist_ok=True)
    for fname, label, fn in JOURNEY_SCREENS:
        d = Dev(); fn(d)
        im = to_image(d)
        im.save(os.path.join(out, "autonomy", fname + ".png"))
        jitems.append((label, im))
    sheet(jitems, 3, "Без телефона: пришло — прочитал — ответил — доставлено").save(
        os.path.join(out, "gallery_autonomy.png"))

    for name, title_, screens in (("relays", "Реле: умные выключатели", RELAY_SCREENS),
                                  ("settings", "Настройки сообщений", SETTINGS_SCREENS),
                                  ("gps", "Моё место: координаты одной кнопкой", GPS_SCREENS)):
        items = []
        os.makedirs(os.path.join(out, name), exist_ok=True)
        for fname, label, fn in screens:
            d = Dev(); fn(d)
            im = to_image(d)
            im.save(os.path.join(out, name, fname + ".png"))
            items.append((label, im))
        sheet(items, 3, title_).save(os.path.join(out, "gallery_%s.png" % name))

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
