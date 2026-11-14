Прошивка companion для **Heltec T114, V3 и V4** на основе [TERMIN36/MeshCore](https://github.com/TERMIN36/MeshCore). Как пользоваться: [README](https://github.com/vladkondratyev/meshcore#readme).

## Что нового в v1.17.1-0.1.9

- **Клавиатура: новые нажатия.** 1 — вправо, 2 — влево, 3 — вниз, 4 — вверх, 5 — выход без отправки, удержание — ввести клавишу. В списках всё как раньше.
- **Третий экран фраз:** `погода сегодня`, `погода завтра`, `Курс`, `луна` (`weather today`, `weather tomorrow`, `rate`, `moon`). Длинная фраза занимает всю строку и видна целиком.
- **Свои фразы командами** в контакте Console или по кабелю: `set phrase 15 погода сегодня` заменяет фразу № 15, `set phrase 15` возвращает стандартную, `phrases 1|2|3` показывает фразы с номерами. Сохраняются в памяти устройства.

![Фразы](https://raw.githubusercontent.com/vladkondratyev/meshcore/main/docs/screens/gallery_write_ru.png)

## Что было в 0.1.8

- `RE @Алексей` в заголовке при ответе. Страница **Sent**: отправленные с устройства, кому, на что и статус.

![Sent](https://raw.githubusercontent.com/vladkondratyev/meshcore/main/docs/screens/gallery_sent.png)

## Что было в 0.1.7

- Ответ на входящее удержанием, повтор отправки при `No reply`, последние адресаты вверху списка Write, число непрочитанных `✉3` в заголовке.

![Ответ с устройства](https://raw.githubusercontent.com/vladkondratyev/meshcore/main/docs/screens/gallery_reply.png)

## Отличия от TERMIN36

- **Бегущая строка (Msg scroll)** — включена сразу, выключается долгим нажатием на странице Msg scroll.
- **Write** — сообщение с устройства в канал или избранному контакту: готовые фразы или свой текст, русская и английская клавиатура, отметка `Delivered` для личных, ответ на входящее, список отправленных (Sent).

## Какой файл брать

| Плата | Файл |
|---|---|
| Heltec T114 | `Heltec_t114_companion_radio_ble…zip` |
| Heltec V3 | `Heltec_v3_companion_radio_ble…-merged.bin` для первой установки, `.bin` для обновления |
| Heltec V4 | `heltec_v4_companion_radio_ble…-merged.bin` для первой установки, `.bin` для обновления |

Сборки `_ble` — подключение к приложению по Bluetooth, `_usb` — по USB, `_wifi` — по Wi‑Fi (только V3 и V4).

В Wi‑Fi-сборках сеть задаётся при сборке: в готовых файлах это `myssid` / `mypwd`, порт TCP `5000`.
