# Нововведения в `mini-kernel/main.c`

Документ фиксирует, что было добавлено в текущую финальную версию `mini-kernel/main.c` по сравнению с ранним состоянием, когда ядро умело выводить только одну строку через framebuffer.

## 1. Общая идея изменений

Раньше mini-kernel после `ExitBootServices()` мог очистить экран и вывести одну строку:

```c
screen_draw_string(&info->video, 100, 200, "Hello World!!!! &!@#$");
```

Теперь реализована небольшая диагностическая status panel ядра. Она собирается в текстовый буфер `status_buffer`, а затем одним вызовом выводится во framebuffer:

```c
mark_current_cpu_alive();

screen_clear(&info->video, COLOR_BACKGROUND);

build_kernel_status_buffer();

screen_draw_text(&info->video, STATUS_X, STATUS_Y, status_buffer);
```

Главная архитектурная особенность финальной версии: текст сначала формируется в памяти, а потом отрисовывается одним проходом. Это устойчивее, чем много отдельных вызовов `screen_draw_string()` подряд.

---

## 2. Таблица изменений

| Было | Что реализовали | Где реализовано в коде |
|---|---|---|
| Выводилась только одна строка через `screen_draw_string()` | Добавлен многострочный вывод с обработкой `\n` | Функция `screen_draw_text(VideoBufferInfo *video, u64 x, u64 y, const char *s)` |
| Координаты вывода задавались вручную числами `100`, `200` | Добавлены именованные константы позиции status panel | `#define STATUS_X 100`, `#define STATUS_Y 120`, `#define TEXT_LINE_GAP 8` |
| Цвет текста был напрямую записан числом `0x0000FF00` | Добавлены именованные константы цветов | `#define COLOR_BACKGROUND 0x00000000`, `#define COLOR_TEXT 0x0000FF00` |
| Не было общего текстового буфера | Добавлен буфер для формирования status panel | `#define STATUS_BUFFER_SIZE 2048`, `static char status_buffer[STATUS_BUFFER_SIZE];` |
| Строка сразу рисовалась во framebuffer | Добавлена буферная схема: сначала сборка текста, потом вывод | `build_kernel_status_buffer()` + `screen_draw_text(..., status_buffer)` |
| Не было простого аналога logger-а | Добавлен buffered logger на базе макросов записи в `status_buffer` | Макросы внутри `build_kernel_status_buffer()`: `BUF_PUTC`, `BUF_PUTS`, `BUF_PUT_DEC`, `BUF_PUT_HEX` |
| Не было вывода чисел | Добавлен вывод десятичных чисел | Макрос `BUF_PUT_DEC(value_expr)` внутри `build_kernel_status_buffer()` |
| Не было вывода адресов в hex | Добавлен вывод 64-битных значений в hex-формате | Макрос `BUF_PUT_HEX(value_expr)` внутри `build_kernel_status_buffer()` |
| Не выводился адрес framebuffer | Добавлена строка `FRAMEBUFFER: ...` | `BUF_PUT_HEX(info->video.buffer)` |
| Не выводился адрес структуры `SystemInfo` | Добавлена строка `SYSTEMINFO: ...` | `BUF_PUT_HEX((u64)info)` |
| Не выводилось разрешение экрана | Добавлены строки `WIDTH` и `HEIGHT` | `BUF_PUT_DEC(info->video.width)` и `BUF_PUT_DEC(info->video.height)` |
| Не было диагностики CPU в status panel | Добавлен вывод количества активных/доступных CPU | `CPU COUNT: started/available`, где `started` считается через `count_alive_processors()`, а `available` берётся из `info->processors.size` |
| Не было отметки реально вошедшего CPU | BSP отмечает себя как активный CPU | `mark_current_cpu_alive()` в `main()` |
| Не было массива состояния CPU | Добавлен массив отметок активных CPU | `static volatile u8 cpu_alive[256];` |
| Не было функции подсчёта активных CPU | Добавлена функция подсчёта CPU, реально дошедших до kernel-кода | `u8 count_alive_processors(void)` |
| Не выводился APIC ID BSP | Добавлена строка `BSP APIC ID: ...` | `BUF_PUT_DEC(get_self_apic_id())` |
| Не выводился список APIC ID | Добавлена строка `APIC IDS: ...` | Цикл по `info->processors.ids[i]` внутри `build_kernel_status_buffer()` |
| Не было явного статуса запуска AP | Добавлена строка `AP STARTUP: DISABLED/ENABLED` | Проверка переменной `ap_startup_was_called` внутри `build_kernel_status_buffer()` |
| AP startup-код был отдельным старым функционалом | Код запуска AP сохранён, но в финальной status panel не вызывается | Функции `start_ap()` и `startup_processors()` оставлены, но вызов в `main()` закомментирован |

---

## 3. Основные новые блоки кода

### 3.1. Константы оформления

Добавлены константы:

```c
#define STATUS_BUFFER_SIZE 2048

#define COLOR_BACKGROUND 0x00000000
#define COLOR_TEXT       0x0000FF00

#define STATUS_X 100
#define STATUS_Y 120
#define TEXT_LINE_GAP 8
```

Они заменяют разрозненные числовые значения в коде и делают назначение параметров понятнее.

---

### 3.2. Многострочный вывод

Реализована функция:

```c
void screen_draw_text(VideoBufferInfo *video, u64 x, u64 y, const char *s)
```

Она отличается от `screen_draw_string()` тем, что понимает символ переноса строки `\n`:

```c
if (*s == '\n') {
  x = start_x;
  y += FONT_HEIGHT + TEXT_LINE_GAP;
  ++s;
  continue;
}
```

Благодаря этому status panel можно хранить как один большой текст с переносами строк.

---

### 3.3. Buffered logger

Добавлен буфер:

```c
static char status_buffer[STATUS_BUFFER_SIZE];
```

И функция сборки:

```c
void build_kernel_status_buffer(void)
```

Внутри неё используются макросы:

```c
BUF_PUTC(ch)
BUF_PUTS(str)
BUF_PUT_DEC(value_expr)
BUF_PUT_HEX(value_expr)
```

Смысл: status panel не рисуется по частям напрямую во framebuffer. Сначала весь текст собирается в `status_buffer`, затем выводится одним вызовом:

```c
screen_draw_text(&info->video, STATUS_X, STATUS_Y, status_buffer);
```

---

### 3.4. Вывод decimal-чисел

Макрос:

```c
BUF_PUT_DEC(value_expr)
```

используется для вывода:

```text
WIDTH
HEIGHT
CPU COUNT
BSP APIC ID
APIC IDS
```

Он вручную переводит число в десятичные символы без `printf`, `malloc`, `strlen` и стандартной библиотеки C.

---

### 3.5. Вывод hex-значений

Макрос:

```c
BUF_PUT_HEX(value_expr)
```

используется для вывода:

```text
KERNEL ENTRY
AP TRAMPOLINE
BSP STACK
SYSTEMINFO
FRAMEBUFFER
```

Примеры строк status panel:

```text
KERNEL ENTRY: 0x0000000000002000
AP TRAMPOLINE: 0x0000000000001000
BSP STACK: 0x0000000000100000
SYSTEMINFO: 0x...
FRAMEBUFFER: 0x...
```

---

## 4. CPU-информация

### 4.1. Доступные CPU

Общее количество CPU берётся из структуры:

```c
info->processors.size
```

Список APIC ID берётся из:

```c
info->processors.ids[i]
```

Эти данные заранее заполняет UEFI-загрузчик `LabExitBs` до `ExitBootServices()`.

---

### 4.2. Реально активные CPU в финальной версии

Добавлен массив:

```c
static volatile u8 cpu_alive[256];
```

BSP отмечает себя вызовом:

```c
mark_current_cpu_alive();
```

Функция:

```c
u8 count_alive_processors(void)
```

считает, сколько CPU из списка `info->processors.ids[]` реально отметились в `cpu_alive[]`.

В финальной стабильной версии AP startup отключён, поэтому ожидаемый формат:

```text
CPU COUNT: 1/4
AP STARTUP: DISABLED
```

Здесь:

```text
1 — BSP, который реально выполняет текущий mini-kernel код;
4 — общее количество CPU, найденное загрузчиком и переданное в SystemInfo.
```

---

## 5. Что сохранено, но не используется в финальном сценарии

Функции запуска AP сохранены:

```c
void start_ap(u8 ap_apic_id, u32 init_code_entry)
void startup_processors(void)
```

Но в `main()` вызов оставлен закомментированным:

```c
// if (get_self_apic_id() == 0) {
//   startup_processors();
// }
```

Причина: финальная status panel должна стабильно рисоваться одним процессором — BSP. Реальный запуск AP оставлен как отдельный старый функционал проекта, но не включён в финальную версию status panel.

---

## 6. Итоговый сценарий работы `main()`

Финальная логика `main()`:

```c
int main() {
  // if (get_self_apic_id() == 0) {
  //   startup_processors();
  // }

  mark_current_cpu_alive();

  screen_clear(&info->video, COLOR_BACKGROUND);

  build_kernel_status_buffer();

  screen_draw_text(&info->video, STATUS_X, STATUS_Y, status_buffer);

  return 0;
}
```

То есть порядок такой:

1. BSP отмечает себя как активный CPU.
2. Экран очищается.
3. Status panel собирается в `status_buffer`.
4. Весь буфер одним вызовом выводится во framebuffer.

---

## 7. Итоговое отличие от старой версии

Старая версия:

```text
mini-kernel мог вывести одну фиксированную строку.
```

Новая версия:

```text
mini-kernel формирует многострочную диагностическую панель состояния ядра,
умеет выводить строки, decimal-числа, hex-адреса,
показывает framebuffer, SystemInfo, разрешение экрана,
CPU count в формате active/available, BSP APIC ID и список APIC ID.
```

Финальная реализация остаётся полностью bare-metal: после `ExitBootServices()` не используются UEFI Boot Services, `Print`, файлы, `malloc`, `printf` или стандартная библиотека C.
