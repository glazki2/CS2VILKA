# CSVILKA

Серверный античит для Counter-Strike 2 (C++, Metamod:Source 2.x, Windows x64 / Linux x64).

CSVILKA анализирует прицеливание, выстрелы, движение, нажатия кнопок и клиентские настройки, которые игроки отправляют серверу. Игрокам ничего устанавливать не нужно.

## Главное отличие: защита от ложных банов

Статистический детект (аим, движение, тайминги) сам по себе **не банит**. Бан отправляется только когда:

- набралось `csvilka_ban_confirmations` (по умолчанию **2**) независимых детектов,
- между детектами минимум 30 секунд (одна серия выстрелов не считается дважды),
- все они попали в окно `csvilka_confirmation_window` (по умолчанию **1800 с**); история хранится по SteamID64 и не сбрасывается переподключением.

Сразу банят только детерминированные проверки, где ошибка исключена: `DLL INJECTION`, `INVALID CVAR`, `INVALID INPUT`.

Дополнительно:

- `csvilka_observe_mode 1` — только детект и отчёты, без наказаний. Рекомендуется на первые дни на новом сервере.
- Сетевое вето: при высоком пинге, джиттере, потере пакетов или choke наказание не отправляется.
- `DESUBTICKING`, `NULLS`, `SUBTICK SPAM` дают только кик.
- `csvilka_whitelist` — игроки детектятся, но никогда не наказываются.
- Автообновление выключено по умолчанию.

## Журнал и команды администратора

- `csvilka_detection_log 1` — каждый детект пишется в `addons/csvilka/logs/detections-ГГГГ-ММ-ДД.log`: время, SteamID64, ник, детект, итог, улики.
- `csvilka_detection_command` — команда на каждый детект, даже без наказания (например, запись демо или уведомление админам).
- `csvilka_evidence [steamid64]` — показать накопленные подтверждения.
- `csvilka_pardon <steamid64>` — сбросить подтверждения игрока (если детект оказался ложным).

## Модули

| Группа | Детекты |
|---|---|
| Аим и точность | AIMBOT, AIMLOCK, SILENTAIM, TRIGGERBOT, INHUMAN ACCURACY, IRREGULAR BEHAVIOR, DOUBLETAP |
| Движение | BHOP, HYPERSCROLL, AUTOSTRAFE, NULLS, DESUBTICKING, SUBTICK SPAM |
| Клиент | ANTIAIM, DLL INJECTION, INVALID CVAR, INVALID INPUT, NAMECHANGER |

Каждый модуль включается/выключается в `cfg/csvilka.cfg` (`csvilka_<модуль>_enabled`) и имеет debug-режим (`csvilka_<модуль>_debug`).

## Установка

1. Установите Metamod:Source 2.x на сервер CS2.
2. Распакуйте пакет в корень сервера (пакет начинается с папки `game`).
3. Настройте `game/csgo/cfg/csvilka.cfg`.
4. Запустите сервер, выполните `meta list`, затем `csvilka_status`.

Команды наказания по умолчанию рассчитаны на CS2-SimpleAdmin (`css_addban`, `css_kick`). Для другого админ-плагина замените `csvilka_punishment_command` и `csvilka_kick_command`. Плейсхолдеры: `{steamid64}`, `{userid}`, `{detection}`.

Отчёты в Discord: `csvilka_webhook_url`.

## Сборка

```sh
git clone --recursive https://github.com/glazki2/cs2vilka.git
cd cs2vilka
./build-linux.sh        # Linux, нужен Docker (Steam Runtime 3 SDK)
./build-windows.ps1     # Windows, нужен Visual Studio 2022 + Python 3.8+
```

Готовый пакет появится в `<папка сборки>/package/game`.

## Лицензия

GNU Affero General Public License v3.0 — см. [LICENSE](LICENSE) и [NOTICE](NOTICE). Зависимости — под своими лицензиями, см. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
