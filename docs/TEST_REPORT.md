# Звіт тестів RIDDIM ROOTS 0.3.0

Згенеровано `tools/report.py` з фактичних логів (`build/`) і звітів збірки (`dist/`).

## Збірка

- Прошивка: `dist/RIDDIM_ROOTS_v0.3.0.fwsc`, SHA-256 `8bb2312805921eac8e9400926aae4737606bf656d387eb086d2f86185d4f0aea`
- Перевірки SDK: passed = True, app image 443208 B (ліміт пакувальника 453 921 B), runtime heap 172844 B (мінімум 49152), аудіо-колбеки в SRAM = True
- Пакет: fits = True, user partition 536576 B, VM 40960 B
- `rr_fx_process` перевірено в діапазоні адрес внутрішньої SRAM (tools/Build.ps1).

## Хост-тести ядра (TinyCC)

- **5 125 446 перевірок  0 помилок**
- evolve  10: 0 mutations / 12 runs x 8 phrases
- evolve  40: 94 mutations / 12 runs x 8 phrases
- evolve  70: 360 mutations / 12 runs x 8 phrases
- evolve 100: 533 mutations / 12 runs x 8 phrases

Детерміновані seed (хеш 64 тактів, seed 42) для порівняння регресій:

```
seed42 SKA          3914d24b
seed42 ROCKSTEADY   4f7c19dc
seed42 ROOTS        4bb5919d
seed42 STEPPERS     412510c0
seed42 DUB          5583bcdc
seed42 RUB-A-DUB    e364f477
seed42 DANCEHALL    7e40e16f
seed42 RAGGA JUNGL  d370e37d
seed42 DUB TECHNO   2bc41313
seed42 DEEP DUBSTP  3e8a78b9
seed42 DUB NOISE    d9d6ba2c
```

## Нативний приймальний тест (емулятор FM-1, справжній .fwsc)

- **142 перевірок пройдено**, 225.0 емульованих секунд (239 с реального часу)
- Пакет під тестом: SHA-256 `8bb2312805921eac8e9400926aae4737606bf656d387eb086d2f86185d4f0aea`
- STOP: жива нота звучить без бенду (RMS 0.0313); PLAY: RMS 0.0575, пік 0.831
- BPM з HOME, HOME+енкодер, CC3 зі згладжуванням, повернення: фінальний 72
- Переходи стилів без зупинки (аудіо перевірялось кожні 0,5 с):
  - STEPPERS: 4 тактів, BPM 77 (пік 77), шлях темпу [72, 72, 73, 73, 73, 73, 75, 75, 75, 76, 76, 76]
  - DUB TECHNO: 4 тактів, BPM 122 (пік 122), шлях темпу [78, 78, 78, 81, 88, 88, 94, 99, 99, 110, 110, 119]
  - RAGGA JUNGLE: 4 тактів, BPM 164 (пік 164), шлях темпу [122, 127, 132, 139, 143, 153, 156]
  - ROOTS: 3 тактів, BPM 72 (пік 82), шлях темпу [75, 82, 82, 81, 80, 80, 78, 77, 77, 76]
- FREEZE BASS + EVOLVE 100 (≈70 с, DUB TECHNO): бас незмінний, змінилось 7 інших компонентів, мутацій 26
- MIDI flood 1500 нот: без залиплих нот; втрачено повідомлень (лічильник) 0
- Довгий прогін 61 с з випадковими контролами: {'underruns': 0, 'overruns': 0, 'bars': 102, 'mutations': 15, 'peak': 0.831268310546875, 'shed': 0}
- Також: 8 фейдерів × 4 значення, банк 2 через Program Change, 8 падів з velocity, сирена, ribbon/tilt/bend/aftertouch, sustain, режими CHORD/SPLIT, октави, усі 10 сторінок (знімки `build/emu-page-N.bmp`), команди диригента, FX з панелі, RAM STORE/RECALL, FLASH SAVE/LOAD.

## Анімація лева

- 13 перевірок: SLEEP, PLAY (≥3 пози: 40), MIDI, MUTATE, FX, DROP, DANCE, FREEZE (поза незмінна)
- Передано пікселів на LCD: STOP 21760/с, PLAY 142629/с (повний екран = 57 600)
- Знімки: `build/lion-*.bmp`; underruns/overruns під час анімації = 0

## Аудіо 11 стилів (8 с кожен, `build/style-NN.wav`)

| Стиль | BPM | RMS | Пік | Кліпінг | Underruns |
|---|---|---|---|---|---|
| SKA | 150 | 0.1923 | 0.831 | 0 | 0 |
| ROCKSTEADY | 96 | 0.1322 | 0.831 | 0 | 0 |
| ROOTS | 72 | 0.0869 | 0.831 | 0 | 0 |
| STEPPERS | 78 | 0.1503 | 0.831 | 0 | 0 |
| DUB | 70 | 0.0872 | 0.831 | 0 | 0 |
| RUB-A-DUB | 84 | 0.0955 | 0.831 | 0 | 0 |
| DANCEHALL | 96 | 0.1099 | 0.831 | 0 | 0 |
| RAGGA JUNGLE | 164 | 0.2265 | 0.831 | 0 | 0 |
| DUB TECHNO | 122 | 0.1748 | 0.831 | 0 | 0 |
| DEEP DUBSTEP | 140 | 0.1631 | 0.831 | 0 | 0 |
| DUB NOISE | 90 | 0.2885 | 0.831 | 0 | 0 |

Пік = стеля лімітера (0,95 FS) × ручка MASTER у тесті (900/1023).

## Не перевірено

- Реальне залізо FM-1, тип TRS, кабель і Vortex фізично, прослуховування на динаміках. Див. `docs/LIMITATIONS.md`.
