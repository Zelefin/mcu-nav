# Як долучитися до розробки (CONTRIBUTING)

Цей документ описує, як налаштувати робоче середовище для **nav-mcu** на чистій
Linux-системі та як ми працюємо в команді: від ідеї до влитого Pull Request.

В основі нашого процесу — набір інженерних скілів
[`mattpocock/skills`](https://github.com/mattpocock/skills), якими користується
кодовий агент **Codex**. Скіли в Codex викликаються явно — через меню `/skills`
або згадку скіла через `$` (наприклад `$grill-me` чи `$implement`); вони
допомагають загострювати дизайн, тримати архітектуру чистою та перетворювати
ідеї на задачі й PR.

> Перед написанням коду обовʼязково прочитайте [README.md](README.md) та
> [AGENTS.md](AGENTS.md) — там зафіксовані архітектурні правила ядра `core/`,
> яких не можна порушувати.

---

## Зміст

1. [Передумови (чистий Linux)](#1-передумови-чистий-linux)
2. [Встановлення скілів mattpocock/skills](#2-встановлення-скілів-mattpocockskills)
3. [Збірка та тести](#3-збірка-та-тести)
4. [Командний робочий процес](#4-командний-робочий-процес)
5. [Шпаргалка по gh CLI](#5-шпаргалка-по-gh-cli)
6. [Правила, яких ми дотримуємось](#6-правила-яких-ми-дотримуємось)

---

## 1. Передумови (чистий Linux)

Приклади команд наведені для **Ubuntu / Debian** (`apt`). Для інших дистрибутивів
замініть менеджер пакетів (`dnf`, `pacman`, `zypper`) на відповідний.

### 1.1. Системні пакети для збірки

Проєкт — це портативна прошивка на C11 із хост-тестами на CMake/CTest та
допоміжними інструментами на Python.

```bash
sudo apt update
sudo apt install -y build-essential cmake git python3 python3-pip
```

### 1.2. Node.js (потрібен для `skills` та Codex)

Скіли встановлюються через `npx`, а сам Codex CLI ставиться через npm, тому
потрібен Node.js (LTS, 20+). Рекомендований спосіб без `sudo` —
через [nvm](https://github.com/nvm-sh/nvm):

```bash
# Встановити nvm
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash
# Перезавантажити shell, потім поставити LTS-версію Node
nvm install --lts
node --version   # переконайтесь, що версія >= 20
```

> Альтернатива через системний пакет:
> `sudo apt install -y nodejs npm` (версія може бути старішою).

### 1.3. GitHub CLI (`gh`)

`gh` потрібен для роботи з Issues та Pull Requests із терміналу та для скілів
`$to-issues`, `$to-prd`, `$triage`.

```bash
# Офіційний репозиторій GitHub CLI для Debian/Ubuntu
sudo mkdir -p -m 755 /etc/apt/keyrings
wget -nv -O- https://cli.github.com/packages/githubcli-archive-keyring.gpg \
  | sudo tee /etc/apt/keyrings/githubcli-archive-keyring.gpg > /dev/null
sudo chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
  | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
sudo apt update
sudo apt install -y gh
```

Після встановлення авторизуйтесь (один раз):

```bash
gh auth login          # оберіть GitHub.com → HTTPS → авторизація у браузері
gh auth status         # перевірка
```

### 1.4. Codex (агент, який запускає скіли)

Скіли цього репозиторію запускає агент **Codex** (OpenAI). Встановіть Codex CLI
глобально через npm:

```bash
npm install -g @openai/codex
codex --version
```

Запустіть агента в корені проєкту:

```bash
codex
```

При першому запуску Codex попросить увійти (ChatGPT-акаунт або API-ключ OpenAI).

Codex шукає скіли в каталозі `.agents/skills/` — від поточної теки вгору до
кореня репозиторію, — тож скіли цього репозиторію він підхоплює без додаткового
налаштування. Викликають їх явно: відкрийте меню `/skills` або згадайте
конкретний скіл через `$`, наприклад `$grill-me`.

---

## 2. Встановлення скілів mattpocock/skills

Скіли вже зафіксовані в [`skills-lock.json`](skills-lock.json) — це «lock-файл»
(аналог `package-lock.json`): він фіксує, які саме скіли, з якого джерела
(`mattpocock/skills`) та з яким хешем очікує проєкт. Завдяки цьому вся команда
отримує **однаковий** набір скілів.

### 2.1. Відновлення з lock-файлу (рекомендовано для команди)

Оскільки [`skills-lock.json`](skills-lock.json) уже є в репозиторії, на чистій
машині достатньо однієї команди — вона відновить усі скіли точно за хешами
(аналог `npm ci`):

```bash
npx skills experimental_install
```

Скіли розпакуються в каталог [`.agents/skills/`](.agents/skills/) — саме звідти
їх читає Codex. Перевірити встановлене:

```bash
npx skills list
```

### 2.2. Оновлення та перевірка скілів

```bash
npx skills check     # подивитись, чи є оновлення
npx skills update    # оновити скіли та перезаписати skills-lock.json
```

> Зміни у `skills-lock.json` та `.agents/skills/` комітьте окремим PR — щоб уся
> команда оновилася синхронно.

---

## 3. Збірка та тести

### 3.1. Рекомендовані VS Code розширення

У репозиторії є файл [`.vscode/extensions.json`](.vscode/extensions.json) із
рекомендованими розширеннями:

- `platformio.platformio-ide` — збірка, прошивання та керування PlatformIO
  середовищами.
- `ms-vscode.vscode-serial-monitor` — перегляд serial output після прошивання.
- `ms-toolsai.jupyter` — робота з ноутбуками для аналізу логів, сценаріїв або
  експериментальних даних.

VS Code запропонує встановити їх після відкриття репозиторію. Локальні
налаштування в `.vscode/` не комітимо; у git має потрапляти лише
`extensions.json`.

### 3.2. Хост-збірка та CMake-тести

Перед будь-яким PR код має збиратися й проходити тести (детальніше — у
[README.md](README.md)):

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Хост-демо та реплей фікстур:

```bash
./build/ports/posix/nav_posix_demo
ctest --test-dir build -V -R replay
```

Для інструментів побудови графіків потрібен matplotlib:

```bash
python3 -m pip install -r requirements-dev.txt
```

### 3.3. Збірка та прошивання ESP32 health-check firmware

Корінь репозиторію є PlatformIO-проєктом для діагностичної ESP32 прошивки, яка
перевіряє wiring та базову працездатність LoRa/SX128x, GPS і QMC5883 compass.
Команди нижче запускайте з кореня репозиторію.

Якщо `pio` не доступний у `PATH`, використовуйте повний шлях до PlatformIO:

```bash
~/.platformio/penv/bin/pio --version
```

#### NodeMCU-32S / NodeMCU-32S Lua

Зібрати firmware:

```bash
pio run -e nodemcu-32s
```

Прошити board:

```bash
pio run -e nodemcu-32s -t upload
```

Відкрити serial monitor на 115200 baud:

```bash
pio device monitor -b 115200
```

#### ESP32-S3-DEVKITC-1

Зібрати firmware:

```bash
pio run -e esp32-s3-devkitc-1
```

Прошити board:

```bash
pio run -e esp32-s3-devkitc-1 -t upload
```

Відкрити serial monitor на 115200 baud:

```bash
pio device monitor -b 115200
```

Якщо ваша версія PlatformIO не знає board id `esp32-s3-devkitc-1`, знайдіть
доступний ID і замініть `board = ...` у [platformio.ini](platformio.ini):

```bash
pio boards espressif32 | grep -i "s3.*devkit"
```

#### Швидка перевірка перед PR

Перед PR, який змінює health-check firmware або `platformio.ini`, зберіть обидва
середовища:

```bash
pio run -e nodemcu-32s
pio run -e esp32-s3-devkitc-1
```

Для повної radio TX/RX перевірки потрібні дві плати з однаковою прошивкою та
однаковими RadioLib build flags. Одна плата може підтвердити radio init і TX,
але RX підтверджується лише коли друга плата передає сумісні packets.

---

## 4. Командний робочий процес

Ми ведемо фічу по конвеєру скілів — від сирої ідеї до влитого PR. Кожен крок
виконується скілом у Codex, а трекером задач слугують GitHub Issues.

```text
  Ідея
   │
   ▼
$grill-me  або  $grill-with-docs        ← загострюємо дизайн (+ ADR/глосарій)
   │
   ▼
$improve-codebase-architecture          ← (за потреби) шукаємо поглиблення архітектури
   │
   ▼
$to-prd  →  $to-issues                  ← PRD і незалежні issue в GitHub
   │
   ▼
git branch + $implement                 ← реалізація через TDD + $review
   │
   ▼
gh pr create  →  рев'ю команди  →  merge
```

### 4.1. Загострення ідеї: `$grill-me` та `$grill-with-docs`

Перш ніж писати код, проженіть ідею через інтервʼю. Агент ставить запитання
**по одному**, спускаючись деревом рішень, поки не лишиться неоднозначностей.

- **`$grill-me`** — чисте інтервʼю, щоб стрес-тестувати план чи дизайн. Нічого
  не записує, лише доводить ідею до ясності.
- **`$grill-with-docs`** — те саме інтервʼю, але **паралельно фіксує рішення**:
  додає терміни в `CONTEXT.md` (глосарій домену) і пропонує оформити вагомі
  рішення як ADR у `docs/adr/`.

**Коли що використовувати:** якщо обговорюєте щось одноразове чи особисте —
`$grill-me`. Якщо рішення вплине на архітектуру, протокол чи доменну мову (а в
цьому проєкті так майже завжди) — беріть **`$grill-with-docs`**, щоб лишився
письмовий слід.

Приклад у Codex:

```text
$grill-with-docs Хочу додати UART-адаптер, що штампує розпарсені NMEA-семпли
системним часом перед інʼєкцією NAV_EVT_LOCAL_GNSS_SAMPLE.
```

### 4.2. Покращення архітектури: `$improve-codebase-architecture`

Коли відчуваєте, що модуль «розповзся» або його важко тестувати, запустіть:

```text
$improve-codebase-architecture
```

Скіл просканує код, знайде **поглиблення** (refactor-и, що перетворюють
поверхневі модулі на глибокі), і збере наочний **HTML-звіт** із діаграмами
«до / після» та силою рекомендації (`Strong`, `Worth exploring`, `Speculative`).
Звіт пишеться в тимчасовий каталог ОС (`$TMPDIR` або `/tmp`) і **не потрапляє**
в репозиторій. Ви обираєте кандидата — і скіл проводить вас грилінгом по його
дизайну.

Користуйтеся спільним словником дизайну (`module`, `interface`, `depth`,
`seam`, `adapter`, `leverage`, `locality`) — він заданий скілом `codebase-design`.
Памʼятайте про правило `core/` з [AGENTS.md](AGENTS.md): жодних залежностей від
платформи в ядрі, увесь платформенний код — у `ports/`.

### 4.3. PRD та issue: `$to-prd` і `$to-issues`

Коли дизайн узгоджено, перетворіть розмову на задачі.

- **`$to-prd`** — синтезує поточне обговорення в PRD (проблема, користувацькі
  історії, рішення з реалізації й тестування) і публікує його в GitHub Issues з
  міткою `ready-for-agent`. Жодного повторного інтервʼю — лише оформлення вже
  обговореного.
- **`$to-issues`** — розбиває план/PRD на **незалежні issue** методом
  «вертикальних зрізів» (tracer bullets): кожна задача — тонкий, але **повний**
  шлях крізь усі шари, який можна продемонструвати окремо. Агент покаже
  нумерований список зрізів із залежностями (`Blocked by`) і опублікує їх у
  правильному порядку.

Усі issue зʼявляться в GitHub. Перевірити з терміналу:

```bash
gh issue list
gh issue view <номер>
```

### 4.4. Реалізація: `$implement`

Візьміть issue в роботу в окремій гілці:

```bash
git switch -c feat/uart-nmea-timestamp      # назва гілки під фічу
```

Потім у Codex:

```text
$implement #<номер-issue>
```

Скіл `$implement`:

- реалізує роботу за PRD/issue, застосовуючи **TDD** на заздалегідь узгоджених
  seam-ах;
- регулярно ганяє окремі тест-файли й типчек, а наприкінці — увесь набір тестів;
- запускає `$review` для самоперевірки;
- комітить результат у поточну гілку.

Памʼятайте про правила з [AGENTS.md](AGENTS.md): оновлюйте тести й документацію
разом зі зміною поведінки; тримайте реплей детермінованим; синхронізуйте
`events.csv`, `replay_config.csv`, `truth.csv` та очікувані звіти.

### 4.5. Pull Request через `gh`

Запуште гілку й відкрийте PR із терміналу:

```bash
git push -u origin feat/uart-nmea-timestamp

gh pr create \
  --title "feat: UART-адаптер штампує NMEA-семпли системним часом" \
  --body "Closes #42

Короткий опис зміни та як її перевірити (збірка + ctest)."
```

- Згадайте `Closes #<номер>` у тілі PR — issue закриється автоматично після
  merge.
- Призначте рецензента й чекайте на рев'ю команди:

```bash
gh pr edit --add-reviewer <username>
gh pr checks            # статус CI
gh pr view --web        # відкрити в браузері
```

Після схвалення зливаємо (зазвичай squash, лишаючи чисту історію):

```bash
gh pr merge --squash --delete-branch
```

---

## 5. Шпаргалка по gh CLI

```bash
# Авторизація
gh auth login
gh auth status

# Issues
gh issue list                          # список відкритих задач
gh issue view <номер>                  # деталі задачі
gh issue create -t "Заголовок" -b "Опис"
gh issue list --label ready-for-agent  # фільтр за міткою тріажу

# Pull Requests
gh pr create                           # створити PR (інтерактивно)
gh pr list                             # список PR
gh pr view <номер>                     # деталі PR
gh pr checks <номер>                   # статуси CI
gh pr review <номер> --approve         # схвалити
gh pr merge <номер> --squash --delete-branch
```

---

## 6. Правила, яких ми дотримуємось

- **Архітектура.** Тримайте [`core/`](core/) портативним C11; жодних ESP-IDF,
  STM32 HAL, Arduino, FreeRTOS, POSIX чи драйверів — увесь платформенний код у
  [`ports/`](ports/). Повний перелік — у [AGENTS.md](AGENTS.md).
- **Потік даних.** Дотримуйтесь моделі «подія на вхід → snapshot/log на вихід».
  Ядро володіє своїм внутрішнім станом.
- **Тести й документація.** Будь-яка зміна поведінки супроводжується тестами та
  оновленням відповідних документів у [`docs/`](docs/).
- **Детермінізм.** Реплей і сценарії мають лишатися відтворюваними; не додавайте
  випадковість у `nav_replay`.
- **Скіли — частина процесу.** Не пишіть фічу «з голови», коли можна загострити
  її через `$grill-with-docs`, оформити через `$to-issues` і реалізувати через
  `$implement`. Так дизайн, домен і задачі лишаються синхронізованими для всієї
  команди.

Дякуємо за внесок у nav-mcu! 🛰️
