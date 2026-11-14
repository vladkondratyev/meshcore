Прошивка companion для **Heltec T114, V3 и V4** на основе [TERMIN36/MeshCore](https://github.com/TERMIN36/MeshCore). Как пользоваться: [README](https://github.com/vladkondratyev/meshcore#readme).

## Отличия от TERMIN36

- **Бегущая строка (Msg scroll).** Новое сообщение проходит через экран крупными точками, затем открывается обычное окно. Включается долгим нажатием на странице Msg scroll, выбор сохраняется. Размер точек, скорость и число проходов задаются флагами сборки.
- **Write — сообщение с устройства.** Канал или избранный контакт, готовые фразы `ok`, `ping`, `yes`, `no`, `hello`, `goodbye` или свой текст на клавиатуре одного экрана. Для личных сообщений — отметка `Delivered`.
- Очередь сообщений для телефона хранится плотнее: около 500 сообщений в той же памяти на T114.

![Бегущая строка](https://raw.githubusercontent.com/vladkondratyev/meshcore/main/docs/screens/ticker/ticker_t114.gif)

![Write](https://raw.githubusercontent.com/vladkondratyev/meshcore/main/docs/screens/gallery_write.png)

## Какой файл брать

| Плата | Файл |
|---|---|
| Heltec T114 | `Heltec_t114_companion_radio_ble…zip` (`_ticker` — бегущая строка включена сразу) |
| Heltec V3 | `Heltec_v3_companion_radio_ble…-merged.bin` для первой установки, `.bin` для обновления |
| Heltec V4 | `heltec_v4_companion_radio_ble…-merged.bin` для первой установки, `.bin` для обновления |

Сборки `_usb` — для подключения к клиенту по USB вместо Bluetooth.
