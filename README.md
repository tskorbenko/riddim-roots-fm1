# RIDDIM ROOTS 0.3.0 — DUB EVOLUTION

Нативна прошивка для **M-VAVE FM-1**: автономний реггі/даб-бенд, яким керуєш як дабовий продюсер — з панелі FM-1 або з **Alesis Vortex Wireless 2** через MIDI-кабель. Повністю нова реалізація на офіційному FM1 Open SDK (не на основі попередніх версій).

**Файл прошивки:** `dist/RIDDIM_ROOTS_v0.3.0.fwsc` (SHA-256 у `dist/SHA256SUMS.txt`).

## Що вміє

- **Безкінечна генеративна музика**: 6 музикантів (drums+perc, bass, skank, organ, horns/lead, piano) + гармонія + аранжування (INTRO/VERSE/CHORUS/DUB/BREAK/BUILD/DROP/OUTRO).
- **11 стилів**: Ska, Rocksteady, Roots, Steppers, Dub, Rub-a-Dub, Digital Dancehall, Ragga Jungle, Dub Techno, Deep Dubstep, Dub Noise — різні граматики, кіти, звуки, мікс, простір.
- **Переходи без STOP**: SMOOTH, DUB MIX, BREAK, HARD SWITCH, MUTANT; 1–16 тактів; double-time/half-time план темпу (72 → 164 BPM без стрибка пульсу).
- **EVOLVE 0–100** з 7 вимірами (rhythm, bass, harmony, instrument, arrangement, timbre, FX), фразова еволюція, відновлення до прийнятого стану.
- **FREEZE / KEEP / REJECT / MUTATE / REBUILD / VERSION / RECALL**, пам'ять M1–M4 і флеш.
- **Диригент**: 35 музичних команд (FILL, BREAKDOWN, DROP, BUILD, BASS SOLO, BUBBLE, ECHO THROW, NEXT VERSION, REWIND…).
- **27 інструментів**: власні DX7-голоси (E.Bass, Upright, Sub, Organ, E.Piano, Piano, Clavinet, Melodica, Brass, Trumpet, Trombone, Steel Pan, Dub Chord…), гітари Karplus-Strong, 303 Wobble/Acid, PSG Noise, 8 драм-китів 808.
- **8 режимів клавіатури** (NORMAL, CHORD, SMART CHORD, SKANK, BASS, SPLIT, SOUND SYSTEM, CONDUCTOR); **STOP ніколи не глушить живу гру**.
- **DUB FX LAB**: 24 ефекти (стрічкова луна, фільтри, фейзер, фленжер, хорус, сатурація, біткраш, tape stop, rewind, stutter, сирена, ейрхорн, лазер, noise, bass drop, echo throw…), DC-блокер і лімітер.
- **BPM з будь-якої сторінки**, tap tempo, MIDI CC з плавним наближенням.
- **Vortex**: 3 банки фейдерів (MIX / EVOLVE / DUB FX), 8 пад-дій, ribbon/tilt/bend/aftertouch/sustain, soft takeover, MIDI Learn, діагностика.
- **Rasta UI**: 10 сторінок, анімований **Лев Юди** з наданого піксель-арту (кивок у такт, сон на STOP, трясіння на MUTATE, танець на зміні стилю, заморожування, реакція на MIDI), часткове перемальовування.

## Документація

- [Керування на FM-1](docs/CONTROLS_UA.md) · [Vortex через MIDI](docs/VORTEX_MIDI_UA.md)
- [Рушій еволюції](docs/EVOLUTION_ENGINE.md) · [Переходи і темп](docs/STYLE_TRANSITIONS.md) · [DUB FX](docs/DUB_FX.md) · [Rasta UI](docs/RASTA_UI.md)
- **[Інструкція користувача](docs/INSTRUKTSIIA_UA.md)**
- [Звіт тестів](docs/TEST_REPORT.md) · [Обмеження](docs/LIMITATIONS.md) · [Прогрес](PROGRESS.md) · [Ліцензії](THIRD_PARTY.md)

## Збірка і тести (Windows, PowerShell)

```powershell
.\tools\Setup.ps1          # SDK і емулятор на зафіксованих ревізіях, тулчейн JieLi 2.5.3 (з перевіркою SHA-256)
.\tools\Build.ps1          # нативна збірка → dist\RIDDIM_ROOTS_v0.3.0.fwsc
.\tools\Test.ps1           # хост-тести ядра + нативний приймальний тест в емуляторі
node tests\lion.mjs        # анімація лева і часткове перемальовування
node tests\styles.mjs      # 11 стилів: WAV у build\style-NN.wav, рівні, кліпінг
python tools\make_demo.py  # браузерне демо в demo\
```

Перегенерація лева (потрібен Pillow): `python tools\lion_assets.py`. Мапінг Vortex у JSON: `python tools\vortex_json.py`.

## Браузерне демо

`python tools\make_demo.py`, потім `python -m http.server 8765 --directory demo` і відкрий <http://localhost:8765/>. Сторінка запускає **той самий `.fwsc`** на емуляторі FM-1 (fm1-emu, MIT) прямо в браузері: Start → Space (PLAY).

## Встановлення на FM-1

Прошивку **не записано** на пристрій автоматично. Встанови її сам через офіційний workbench / updater M-VAVE (<https://open.m-vave.com/fm1-workbench>), попередньо зробивши резервну копію проєктів і семплів. Повернення до стоку — офіційна прошивка V15 з сайту M-VAVE.

## Ліцензія

GPL-3.0-only WITH GPL-3.0-linking-exception (див. `LICENSE.md`); бінарні компоненти SDK — за умовами M-VAVE (`vendor/fm1_open/THIRD_PARTY.md`). Деталі — `THIRD_PARTY.md`.
