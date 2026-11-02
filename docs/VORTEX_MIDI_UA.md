# Alesis Vortex Wireless 2 → FM-1 через MIDI-кабель

Мета: один раз налаштувати пресет у Alesis Vortex Wireless 2 Editor, потім грати без комп'ютера.

## Підключення

```
Vortex MIDI OUT (5-pin DIN) → MIDI-кабель → DIN→3,5 мм TRS адаптер → FM-1 MIDI IN (TRS)
```

- FM-1 приймає TRS MIDI на UART1 (31250 бод) — це підтверджено вихідниками SDK (`fm1_io_trs_midi_enabled`) і fm1-dev-wiki. Вихідного MIDI через TRS немає, тож Vortex **не отримує** зворотного зв'язку (фейдери не рухаються, LED не синхронізуються).
- **Тип TRS (A чи B) для гнізда FM-1 офіційно не задокументований.** Більшість подібних пристроїв — Type A (Tip→DIN 5, Ring→DIN 4, Sleeve→DIN 2). Перевір: якщо на сторінці MIDI (кнопка GLO) не з'являються повідомлення при натисканні клавіш Vortex — спробуй адаптер іншого типу. Неправильний тип не пошкоджує пристрій електрично за стандартом MIDI (струмова петля), але сигнал не пройде.
- Vortex живиться від батарей або USB-БЖ; USB-приймач і комп'ютер під час гри не потрібні. У Editor увімкни передачу на DIN (**MIDI to DIN / DIN Out = On**).

## Пресет «RIDDIM ROOTS» для Vortex Editor

Це **власні призначення проєкту**, а не заводські CC Alesis. Прошивка і пресет мають збігатися — тому створи пресет саме так (або змінюй через MIDI Learn на FM-1).

| Контрол Vortex | Тип | Канал | Номер | Діапазон / режим | Функція у FM-1 |
|---|---|---|---|---|---|
| Клавіатура | Notes | 1 | — | Transpose 0 | жива гра (режим LIVE) |
| F1–F8 | CC | 1 | 20–27 | 0–127 | залежно від банку (нижче) |
| Pad 1–8 | Note | 10 | 36–43 | velocity, Note Off при відпусканні | дабові дії (нижче) |
| Pitch wheel | Pitch Bend | 1 | — | — | живий інструмент + висота сирени |
| Ribbon bank 1 | CC | 1 | 74 | 0–127, Latch | FILTER (DJ: <64 LP, >64 HP) |
| Ribbon bank 2 | CC | 1 | 71 | 0–127, Latch | DELAY FEEDBACK |
| Ribbon bank 3 | CC | 1 | 16 | 0–127, Latch | MORPH / EVOLVE |
| Tilt | CC | 1 | 17 | 0–127 | CHAOS (інтенсивність дабових ефектів; мертва зона ±6 біля 64) |
| Volume slider | CC | 1 | 7 | 0–127 | MASTER |
| Sustain | CC | 1 | 64 | — | сустейн живих нот |
| Aftertouch | Channel Pressure | 1 | — | — | EXPRESSION (відкриває фільтр) |
| Mod (CC1) | CC | 1 | 1 | — | EXPRESSION |
| Preset Up/Down | Program Change | 1 | 110 / 111 / 112 | Send On Load = Yes | банк фейдерів 1 / 2 / 3 |
| (опційно) | CC | 1 | 3 | 0–127 | BPM (діапазон 50–200, змінюється на SYSTEM) |
| (опційно) | CC | 1 | 9 | 0–127 | STYLE (11 зон) |
| (опційно) | CC | 1 | 118 / 117 / 116 | ≥64 | PLAY / STOP / TAP |
| (опційно) | CC | 1 | 119 | 0–35 | команда диригента № |
| Інші Program Change | PC | 1 | 0–26 | — | звук LIVE A |

MIDI Start/Stop (FA/FC) теж запускають/зупиняють бенд. MIDI Clock ігнорується (внутрішній годинник).

### Банки фейдерів (Program Change 110/111/112)

| Фейдер | Банк 1 MIX | Банк 2 EVOLVE | Банк 3 DUB FX |
|---|---|---|---|
| F1 | рівень BASS | мутація RHYTHM | FILTER |
| F2 | рівень DRUMS | мутація BASS | DELAY FEEDBACK |
| F3 | рівень SKANK | мутація HARMONY | DELAY TIME (1/32T…1/4) |
| F4 | рівень KEYS (organ + piano) | мутація INSTRUMENT | REVERB SIZE |
| F5 | рівень HORNS/LEAD | мутація ARRANGEMENT | FX SELECT |
| F6 | рівень PERC (conga, shaker, cowbell) | мутація TIMBRE | FX AMOUNT |
| F7 | DELAY SEND | мутація FX | ENERGY |
| F8 | REVERB SEND | GLOBAL EVOLVE | DUB |

**Soft takeover (pickup):** після зміни банку фейдер почне діяти, коли пройде через поточне значення параметра (навіть швидкий рух через значення ловиться). Вимкнути: MIDI → K3 = JUMP.

### Пади

| Пад | Нота (кан. 10) | Дія | Режим |
|---|---|---|---|
| 1 | 36 | Dub siren | поки тримаєш; гучність = velocity; висота = pitch bend |
| 2 | 37 | Echo throw (skank у луну) | натискання |
| 3 | 38 | Drum fill | натискання (квантується до долі) |
| 4 | 39 | Dropout: бенд глушиться поки тримаєш, на відпусканні — fill і повернення | momentary |
| 5 | 40 | Rewind: тримай — спін назад; відпусти — фраза з початку | momentary |
| 6 | 41 | New dub version (той самий риддім, нова версія) | натискання |
| 7 | 42 | Mutate усіх незаморожених | натискання |
| 8 | 43 | Freeze/unfreeze частини, вибраної на сторінці FREEZE | toggle |

Пади не створюють «залиплих» ефектів: кожен momentary-ефект відпускається на Note Off; Panic (SYSTEM → PANIC або CC123) знімає все.

### Октави Vortex

Кнопки октави Vortex транспонують ноти на самому Vortex. На FM-1 октава LIVE (OCT−/OCT+) окрема — щоб не було подвійного транспонування, залиш її в 0 (дисплей LIVE: OCTAVE 0).

### Зони клавіатури

Для поділу бас/лід використовуй режим **SPLIT** на FM-1 (точка спліту — LIVE → K2, за замовчуванням C4). Якщо Vortex сам розділяє клавіатуру на 2 канали — FM-1 приймає ноти з будь-якого каналу, крім пад-нот 36–43 на каналі 10.

## Налаштування в Alesis Vortex Wireless 2 Editor (вручну)

1. Підключи Vortex USB-кабелем до комп'ютера, запусти Editor, **Retrieve Preset** і збережи резервну копію поточних пресетів.
2. Обери вільний пресет, назви його `RIDDIM ROOTS`.
3. Keyboard: MIDI Channel **1**, Transpose **0**, MIDI to DIN **On**; Program Change on load **On**, Program **110**.
4. Faders 1–8: Type CC, Channel 1, CC **20…27**, Min 0, Max 127.
5. Pads 1–8: Type Note, Channel **10**, Notes **36…43**, Note Off при відпусканні.
6. Ribbon: 3 банки CC **74 / 71 / 16**, Channel 1, Release **Latch**.
7. Tilt: CC **17**, Channel 1. Volume: CC **7**. Sustain: CC 64. Aftertouch: On.
8. Створи ще два пресети-копії з Program **111** і **112** (банки EVOLVE і DUB FX) — перемикаючи пресети Up/Down, перемикаєш банк фейдерів FM-1.
9. **Send Preset** на Vortex. Це дія власника; проєкт нічого не записує у Vortex автоматично.

Довідковий машиночитний опис цих призначень: `assets/vortex/RIDDIM_ROOTS_vortex_mapping.json`. Це не файл `.vw2` Editor'а (його формат закритий) — не перейменовуй.

## MIDI Learn на FM-1

GLO → K1 вибери ціль (будь-яка з 46), натисни GLO ще раз, порухай контрол Vortex. Підтримуються CC, ноти, pitch bend, aftertouch, Program Change і будь-який канал; призначення діє в поточному банку. SYSTEM → SAVE MIDI MAP зберігає мапінг у флеш (слот 16), він читається при старті; DEFAULTS повертає заводський мапінг проєкту.

## Діагностика

Сторінка MIDI показує останні 6 повідомлень (hex), лічильники нот/CC і втрачених повідомлень, поточний банк і стан Learn. Крапка MIDI на HOME блимає при кожному вхідному повідомленні.
