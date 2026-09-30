# Spacenerd Master

Мастеринг-плагін для Logic Pro (AU) і VST3-хостів. Universal: Apple Silicon + Intel.

**Ланцюжок:** Input → EQ → Compressor → Saturation (4x) → Stereo → True-peak Limiter → Output
**Метри:** In/Out peak, GR компресора й лімітера, LUFS (Integrated / Short-term / Momentary, BS.1770).

## Збірка
Автоматично в GitHub Actions при кожному push у `main` (або вручну: Actions → Run workflow).
Готові файли: вкладка **Actions** → останній запуск → **Artifacts** → `SpacenerdMaster-macOS`.

## Встановлення
1. Розпакуйте `Spacenerd Master AU.zip`.
2. Finder → Перехід → Перейти до папки → `~/Library/Audio/Plug-Ins/Components` → перетягніть туди `Spacenerd Master.component`.
3. Один раз у Терміналі (зняти карантин з завантаженого файлу):
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Spacenerd\ Master.component
   ```
4. Перезапустіть Logic. Плагін: Audio Units → Spacenerd → Spacenerd Master.
   Якщо не з'явився: Logic → Settings → Plug-in Manager → Reset & Rescan Selection.

## Нотатки
- Подвійний клік по ручці — значення за замовчуванням. Вікно масштабується за кутик.
- Затримка (~3.5 мс) повідомляється хосту, Logic компенсує її автоматично.
- Low Cut і Mono Bass у крайньому лівому положенні = Off.
