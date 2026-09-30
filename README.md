# Spacenerd Plugins

AU (Logic Pro) + VST3, Universal: Apple Silicon + Intel.

## Spacenerd Master — мастеринг
Input → EQ → Compressor → Saturation (Tube / Tape / Soft) → Stereo → True-peak Limiter → Output.
EQ, компресор і сатурація працюють у 4x оверсемплінгу (без «стискання» АЧХ біля 20 кГц і без аліасингу).
- Компресор: soft knee, SC-фільтр, паралельний Mix, **Auto Release** (двоступеневий, менше пампінгу).
- Лімітер: true peak, lookahead 4 мс, двоступеневий реліз (менше спотворень басу).
- Метри: peak, GR, LUFS (BS.1770), максимум true peak на виході.
- **Gain Match**: вирівнює гучність із входом, щоб чесно порівнювати з Bypass. Вимикайте перед експортом.

## Spacenerd Tone — окремі доріжки й шини
Transient → Tube → Tape → Exciter → De-esser, загальний Mix (сухий сигнал вирівняний за затримкою).
- **Tube**: асиметричний тріод (парні гармоніки), Bias, зсув робочої точки від рівня.
- **Tape**: магнітний гістерезис (модель Джайлза–Атертона), швидкість 7.5 / 15 / 30 ips (горб на басу, завал ВЧ), Wow/Flutter.
- **Transient**: Attack / Sustain, не залежить від рівня.
- **Exciter**: гармоніки лише з верхньої смуги.
- **De-esser**: відносний детектор (не треба підганяти поріг під гучність фрази), Listen.
- Лампа і плівка відкалібровані: на -18 dBFS Drive змінює характер, а не гучність.

## Збірка
Автоматично в GitHub Actions при кожному push у `main`.
Готові файли: **Actions** → останній запуск → **Artifacts** → `Spacenerd-Plugins-macOS`.

## Встановлення
1. Розпакуйте `… AU.zip`, перетягніть `.component` у `~/Library/Audio/Plug-Ins/Components`.
2. Один раз для кожного плагіна в Терміналі:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Spacenerd*.component
   ```
3. Перезапустіть Logic: Audio Units → Spacenerd.

## Нотатки
- Подвійний клік — значення за замовчуванням, **Shift + тягнути** — точно. Вікно масштабується.
- Затримка повідомляється хосту, Logic компенсує її сам.
