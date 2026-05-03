# Полный разбор пайплайна запуска mini-kernel через UEFI/edk2

 Весь путь запуска проекта: от старта QEMU и UEFI-прошивки до момента, когда собственный `mini-kernel` получает управление, запускает дополнительные процессоры и рисует полосы на экране.

---

## 0. Главная идея

Главная идея такая:

1. UEFI-загрузчик `LabExitBs.efi` стартует как обычное UEFI-приложение.
2. Он получает от UEFI полезную информацию: видеобуфер, разрешение экрана, список процессоров.
3. Он получает карту памяти и вызывает `ExitBootServices`.
4. Он копирует наш бинарник `kernel` в физическую память по адресу `0x2000`.
5. Он передаёт адрес структуры `SystemInfo` через регистр `RAX`.
6. Он прыгает в `kernel`.
7. Ядро начинает работать уже почти как bare-metal-код.
8. BSP-процессор запускает остальные процессоры через Local APIC.
9. Каждый процессор рисует свою горизонтальную полосу на экране.

---

## 1. Полный пайплайн запуска от самого начала до последнего файла

### Этап 1. Запускается QEMU

Мы запускаем виртуальную машину примерно такой командой:

```bash
qemu-system-x86_64 \
  -machine q35 \
  -cpu qemu64 \
  -smp 4 \
  -m 256M \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
  -drive if=pflash,format=raw,file=/usr/share/edk2/x64/OVMF_VARS.4m.fd \
  -drive format=raw,file=fat:rw:$HOME/uefi-bootloader/uefi_share \
  -serial stdio \
  -monitor none
```

Что здесь важно:

1. `qemu-system-x86_64` запускает x86-64 виртуальную машину.
2. `-machine q35` выбирает современную PC-платформу.
3. `-cpu qemu64` задаёт модель процессора.
4. `-smp 4` создаёт 4 виртуальных процессора.
5. `-m 256M` выделяет 256 МБ оперативной памяти.
6. `OVMF_CODE.4m.fd` — код UEFI-прошивки.
7. `OVMF_VARS.4m.fd` — переменные UEFI, например настройки BootOrder.
8. `fat:rw:$HOME/uefi-bootloader/uefi_share` подключает обычную папку как FAT-диск.

Важно: `OVMF_CODE.4m.fd` и `OVMF_VARS.4m.fd` — разные вещи. `CODE` содержит саму прошивку, `VARS` — переменные. 

---

### Этап 2. Стартует OVMF / UEFI

После запуска QEMU управление получает OVMF — UEFI-прошивка.

Она:

1. инициализирует виртуальное железо;
2. поднимает UEFI Boot Services;
3. подключает файловую систему на FAT-диске;


---

### Этап 3. Запускаем `LabExitBs.efi`

`LabExitBs.efi` — это UEFI-загрузчик для нашего mini-kernel.

Он собирается из папки:

```text
LabExitBs/
├── LabExitBs.inf
├── main.c
└── kernel.inc
```

Он является обычным edk2 UEFI application. Его точка входа:

```c
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable)
```

Именно отсюда начинается наш загрузчик.

---

### Этап 4. `LabExitBs.efi` получает framebuffer через GOP

В `LabExitBs/main.c` есть функция:

```c
EFI_STATUS GetGop(EFI_GRAPHICS_OUTPUT_PROTOCOL **Gop)
```

Она ищет `Graphics Output Protocol`, сокращённо GOP.

GOP нужен, чтобы получить:

1. адрес видеобуфера;
2. ширину экрана;
3. высоту экрана.

В `UefiMain` после вызова `GetGop` делается:

```c
VideoBufferAddress = Gop->Mode->FrameBufferBase;
HorisontalRes = Gop->Mode->Info->HorizontalResolution;
VerticalRes = Gop->Mode->Info->VerticalResolution;
```

Эти значения потом попадут в `SystemInfo` и будут доступны ядру.

---

### Этап 5. `LabExitBs.efi` получает список процессоров через MP Services

В `LabExitBs/main.c` есть структура:

```c
typedef struct _Context {
  EFI_MP_SERVICES_PROTOCOL *mp;
} Context;
```

И функции:

```c
BOOLEAN context_setup_mp(Context *self)
UINTN context_processors_count(Context *self)
EFI_PROCESSOR_INFORMATION context_get_processor_info(Context *self, UINTN index)
```

Они работают с `EFI_MP_SERVICES_PROTOCOL`.

Этот протокол позволяет узнать:

1. сколько процессоров есть;
2. какие у них аппаратные ID;
3. какой процессор является BSP;
4. какие процессоры включены;
5. топологию процессоров.

В `UefiMain` делается:

```c
Context ctx;

if (!context_setup_mp(&ctx)) {
  Print(L"Error while get context!\r\n");
  return EFI_LOAD_ERROR;
}

UINTN proc_count = context_processors_count(&ctx);
info.processors.size = proc_count;

for (UINTN i = 0; i < proc_count; ++i) {
  EFI_PROCESSOR_INFORMATION efi_proc_info =
      context_get_processor_info(&ctx, i);

  info.processors.ids[i] = efi_proc_info.ProcessorId;
}
```

То есть загрузчик заранее готовит для ядра список APIC ID процессоров.

---

### Этап 6. `LabExitBs.efi` получает карту памяти

Перед тем как выйти из Boot Services, UEFI требует получить актуальную карту памяти.

В `LabExitBs/main.c` делается:

```c
Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey,
                           &DescriptorSize, &DescriptorVersion);
```

Первый вызов обычно возвращает:

```text
EFI_BUFFER_TOO_SMALL
```

Это нормально: UEFI сообщает, что буфер слишком маленький, и через `MemoryMapSize` говорит, сколько памяти нужно выделить.

Потом загрузчик выделяет буфер:

```c
Status = gBS->AllocatePool(EfiBootServicesData,
                           MemoryMapSize + 5 * DescriptorSize,
                           (void **)&MemoryMap);
```

И повторяет `GetMemoryMap`.

Важный технический момент:

`MapKey` должен быть свежим. Если после получения карты памяти сделать ещё какие-то операции, которые меняют память, `MapKey` может устареть, и `ExitBootServices` может вернуть ошибку.

---

### Этап 7. `LabExitBs.efi` вызывает `ExitBootServices`

После получения карты памяти вызывается:

```c
Status = gBS->ExitBootServices(ImageHandle, MapKey);
```


До `ExitBootServices` мы находимся в мире UEFI:

1. доступны `gBS`;
2. можно печатать через `Print`;
3. можно открывать файлы;
4. можно использовать протоколы;
5. можно выделять память через Boot Services.

После `ExitBootServices`:

1. Boot Services больше недоступны;
2. нельзя использовать обычные UEFI-сервисы;
3. управление системой переходит нашему коду;
4. ядро должно работать самостоятельно.

Именно поэтому данные вроде framebuffer address, width, height и processor IDs надо собрать заранее.

---

### Этап 8. `LabExitBs.efi` заполняет `SystemInfo`

`SystemInfo` — это структура, которую загрузчик передаёт ядру.

В `LabExitBs/main.c` она описана так:

```c
typedef struct _VideoBufferInfo {
  u64 buffer;
  u64 height;
  u64 width;
} __attribute__((packed)) VideoBufferInfo;

typedef struct _ProcessorsInfo {
  u8 size;
  u8 ids[256];
} __attribute__((packed)) ProcessorsInfo;

typedef struct _SystemInfo {
  VideoBufferInfo video;
  ProcessorsInfo processors;
} __attribute__((packed)) SystemInfo;
```

В `LabExitBs/main.c` есть глобальный объект:

```c
static SystemInfo info;
```

После получения GOP и MP Services туда записывается:

```c
info.video.buffer = VideoBufferAddress;
info.video.height = VerticalRes;
info.video.width = HorisontalRes;
```

А processor IDs уже были записаны раньше:

```c
info.processors.size = proc_count;
info.processors.ids[i] = efi_proc_info.ProcessorId;
```

Технический момент: `__attribute__((packed))` нужен, чтобы компилятор не вставил скрытые padding-байты между полями. Загрузчик и ядро должны видеть структуру одинаково.

---

### Этап 9. `LabExitBs.efi` копирует `kernel` в память по адресу `0x2000`

В загрузчике есть файл:

```text
kernel.inc
```

Это не обычный исходник, а сгенерированный C-массив байтов:

```c
unsigned char kernel[] = { ... };
unsigned int kernel_len = ...;
```

Он создаётся из бинарника `kernel` командой:

```bash
xxd -i kernel > kernel.inc
```

В `LabExitBs/main.c` подключается:

```c
#include "kernel.inc"
```

После этого загрузчик может скопировать встроенный kernel в память:

```c
unsigned char *dst = (void *)0x2000;
for (unsigned int i = 0; i < kernel_len; ++i) {
  dst[i] = kernel[i];
}
```

Адрес `0x2000` здесь критически важен, потому что внутри ядра AP-процессорам потом будет сказано прыгать именно на `0x2000`.

---

### Этап 10. `LabExitBs.efi` передаёт `SystemInfo` через `RAX` и прыгает в kernel

В конце `LabExitBs/main.c` есть inline assembly:

```c
__asm__ __volatile__("mov $0x2000, %%rdi\n\t"
                     "mov %0, %%rax\n\t"
                     "jmp %%rdi\n\t"
                     :
                     : "r"(&info)
                     :);
```

Что здесь происходит:

1. В `RDI` кладётся адрес `0x2000`.
2. В `RAX` кладётся адрес структуры `info`.
3. Выполняется `jmp rdi`, то есть переход на `0x2000`.

Почему не обычный вызов C-функции?

Потому что `entry.nasm` ожидает указатель на `SystemInfo` именно в `RAX`, а не как обычный C-аргумент.

То есть передача управления выглядит так:

```text
RAX = &SystemInfo
jump 0x2000
```

---

### Этап 11. Стартует `entry.nasm`

На адресе `0x2000` лежит начало нашего бинарника `kernel`.

Благодаря `linker.lds` первым в бинарнике расположен код из секции `.entry`, то есть `_entry` из `entry.nasm`.

Код:

```asm
global _entry

extern main
extern info;

section .entry
[bits 64]
_entry:
  ; RAX - указатель на структуру с видеобуфером
  test rax, rax
  jz .skip_setup

      mov [rel info], rax

      mov rsp, 0x100000
      mov rbp, rsp

      jmp .to_main

.skip_setup:

.to_main:
  call main

  jmp $
```

Что происходит:

1. Проверяется `RAX`.
2. Если `RAX != 0`, значит это первый вход от загрузчика.
3. Указатель на `SystemInfo` сохраняется в глобальную переменную `info`.
4. Настраивается стек BSP: `RSP = 0x100000`.
5. Вызывается `main()` ядра.
6. Если `main()` когда-нибудь вернётся, код зависает в `jmp $`.

---

### Этап 12. Выполняется `main.c` ядра на BSP

В ядре есть глобальная переменная:

```c
SystemInfo *info;
```

Она была заполнена в `entry.nasm`:

```asm
mov [rel info], rax
```

Дальше в `main()`:

```c
int main() {
  if (get_self_apic_id() == 0)
    startup_processors();
```

То есть если текущий APIC ID равен 0, процессор считается BSP и запускает остальные процессоры.

---
# Этап 13. BSP запускает дополнительные процессоры

На этом этапе у нас уже работает основной процессор — BSP.

Главная задача функции `startup_processors()` — подготовить специальный стартовый код для AP-процессоров и затем отправить каждому AP сигнал запуска.

В `startup_processors()` происходит:

1. экран очищается;
2.  `processor_init` копируется в память по адресу `0x1000`;
3. читается текущий `CR3` у BSP;
4. значение `CR3` записывается внутрь trampoline-кода;
5. адрес входа в kernel `0x2000` записывается внутрь trampoline-кода;
6. для каждого AP отправляется `INIT/SIPI` через Local APIC.

---

AP-процессор нельзя сразу отправить выполнять обычный C-код ядра.
Нельзя сделать условно так:

```c
start_other_cpu(main);
```
Проблема в том, что AP после пробуждения стартует не в полноценном 64-битном режиме, а в начальном режиме, близком к 16-битному real mode.

А наше ядро уже рассчитано на выполнение в 64-битном long mode.
Поэтому AP сначала должен пройти подготовку:

```text
16-bit стартовый режим
↓
protected mode
↓
PAE
↓
paging
↓
64-bit long mode
↓
переход в kernel
```

Эту подготовку выполняет файл:

```text
processor_init.nasm
```

А в `main.c` он доступен как массив байтов:

```c
processor_init[]
```

Этот массив появляется благодаря файлу:

```c
#include  "processor_init.inc"
```

То есть `processor_init` — это машинный код, который потом будет выполнять AP-процессор.

---

Код:

```c
for (u32 i = 0; i < processor_init_len; ++i) {

((volatile u8 *)0x1000)[i] =  processor_init[i];

}
```

Эта часть делает вещь: копирует байты из массива `processor_init` в физическую память, начиная с адреса `0x1000`.

То есть было так:

```text
processor_init[] находится внутри kernel
```

А после копирования становится так:

```text
0x1000: первый байт processor_init
0x1001: второй байт processor_init
0x1002: третий байт processor_init
...
```

Почему именно `0x1000`?

Потому что AP-процессор после сигнала `SIPI` начинает выполнение не с произвольного адреса, а с адреса, который вычисляется так:

```text
start_address = SIPI_vector * 0x1000
```

В нашем случае мы хотим, чтобы AP начал выполнение с адреса:

```text
0x1000
```

Значит `SIPI_vector` должен быть равен:

```text
0x1000 / 0x1000 = 1
```

Именно поэтому код `processor_init` заранее кладётся по адресу `0x1000`.

---

Код:

```c
u64 cr3 = 0;
__asm volatile("movq %%cr3, %0" : "=r"(cr3) : :);
```

Здесь BSP читает значение регистра `CR3`.

`CR3` — это специальный управляющий регистр процессора.
Он хранит физический адрес верхнего уровня таблиц страниц. Проще говоря:

```text
CR3 показывает процессору, где находятся таблицы страниц.
```

Таблицы страниц нужны для перевода виртуальных адресов в физические.

Например, когда код обращается к какому-то адресу памяти, процессор должен понимать, где этот адрес реально находится в RAM. Для этого и используются таблицы страниц.

BSP уже работает в 64-битном режиме и уже использует правильные таблицы страниц. Поэтому его `CR3` считается правильным.

AP-процессорам нужно использовать те же таблицы страниц, иначе после перехода в long mode они могут не увидеть код ядра, стек или нужные данные.

Поэтому BSP делает так:

```text
1. читает свой CR3;
2. сохраняет его в переменную cr3;
3. записывает это значение внутрь processor_init;
4. AP потом загрузит это значение в свой CR3.
```

---

Код:
```c
*(volatile u64 *)((u8 *)0x1000 + processor_init_len - 16) = cr3;
```

Эта строка записывает значение `cr3` в конец скопированного `processor_init`.
  
В конце `processor_init.nasm` есть два 8-байтных поля:

```asm
pml4_address dq 0
entry_point dq 0
```

`dq` означает **define quadword**, то есть зарезервировать 8 байт. Изначально эти значения равны нулю:

```text
pml4_address = 0
entry_point = 0
```

Но AP-процессору нужны реальные значения.

Первое поле, `pml4_address`, должно хранить адрес таблицы PML4. По сути, это значение, которое AP должен загрузить в `CR3`.

Строка:

```c
*(volatile u64 *)((u8 *)0x1000 + processor_init_len - 16) = cr3;
```

означает:

```text
1. взять адрес начала processor_init в памяти: 0x1000;
2. прибавить длину processor_init;
3. отступить назад на 16 байт;
4. записать туда 8-байтное значение cr3.
```

Почему именно `-16`?

Потому что последние 16 байт `processor_init` выглядят так:

```text
последние 16 байт:
[8 байт pml4_address]
[8 байт entry_point]
```

Если мы отступаем от конца на 16 байт, то попадаем в начало поля `pml4_address`.

После этой записи получается:

```text
pml4_address = cr3
```

И когда AP будет выполнять `processor_init`, он сможет сделать:

```asm
mov eax, [pml4_address]
mov cr3, eax
```
То есть AP загрузит в свой `CR3` те же таблицы страниц, которые использует BSP.

---

Код:

```c
*(volatile u64 *)((u8 *)0x1000 + processor_init_len - 8) = 0x2000;
```

  

Эта строка записывает второе значение в конец `processor_init`.

  

Если предыдущая строка записывала `pml4_address`, то эта записывает:

```asm
entry_point dq 0
```

Почему используется `-8`?

Потому что поле `entry_point` занимает последние 8 байт `processor_init`.

Схема такая:

```text
processor_init в памяти:

0x1000:

машинный код processor_init

...

pml4_address ← находится за 16 байт до конца

entry_point ← находится за 8 байт до конца
```

Значение `0x2000` — это адрес, куда загрузчик `LabExitBs` скопировал основное ядро. То есть после этой строки:

```text
entry_point = 0x2000
```

Теперь AP-процессор после перехода в 64-битный режим будет знать, куда идти дальше.

Итоговый маршрут AP будет таким:

```text
AP стартует с 0x1000
↓
выполняет processor_init
↓
переходит в 64-битный режим
↓
берёт entry_point
↓
прыгает в 0x2000
↓
попадает в kernel
```

---

После этих строк память выглядит примерно так:

```text
0x1000:

processor_init-код

...

pml4_address = CR3 BSP

entry_point = 0x2000

0x2000:

основное kernel-ядро
```

То есть у нас есть два важных адреса:

```text
0x1000 — временный стартовый код для AP-процессоров;
0x2000 — основное ядро, куда AP должен попасть после подготовки.
```

---

После подготовки `processor_init` BSP начинает запускать AP-процессоры.

Логика такая:

```c
for (u8 i = 1; i < count; ++i) {
u8 ap_apic_id = info->processors.ids[i];
start_ap(ap_apic_id, 0x1000);
}
```

`i = 1` используется потому, что процессор с индексом `0` — это обычно сам BSP.

То есть:

```text
ids[0] — BSP;
ids[1] — первый AP;
ids[2] — второй AP;
ids[3] — третий AP.
```

Для каждого AP вызывается функция:

```c
start_ap(ap_apic_id, 0x1000);
```

Она отправляет процессору два сигнала через Local APIC:

```text
INIT
SIPI
```

`INIT` переводит AP в начальное состояние.

`SIPI` говорит AP, с какого адреса начать выполнение.

В нашем случае SIPI указывает на адрес `0x1000`.

---

# Этап 14. AP-процессор стартует с `processor_init.nasm`

Когда BSP отправляет AP-процессору сигнал `SIPI`, AP начинает выполнение с адреса, который вычисляется так:

```text
start_address = SIPI_vector * 0x1000
```

В коде BSP вызывает:

```c
start_ap(ap_apic_id, 0x1000);
```

Внутри `start_ap` из адреса `0x1000` получается SIPI vector:

```text
init_code_entry = 0x1000
init_code_entry >> 12 = 1
vector = 1
```

Сдвиг вправо на 12 бит — это то же самое, что деление на `0x1000`.

```text
0x1000 >> 12 = 1
```

Значит AP стартует с адреса:

```text
1 * 0x1000 = 0x1000
```

Именно поэтому на предыдущем этапе `processor_init` копировался в память по адресу `0x1000`.

---

Адрес `0x2000` — это основное ядро.

Но AP после SIPI ещё не находится в том состоянии, в котором можно выполнять обычный 64-битный код ядра.

Он стартует в начальном режиме, похожем на 16-битный real mode.

А код ядра уже рассчитан на 64-битный режим.

Поэтому AP сначала должен выполнить `processor_init.nasm`.

`processor_init.nasm` — это промежуточный загрузочный код, который подготавливает AP.

---

`processor_init.nasm` выполняет последовательность переходов между режимами процессора:

```text
1. стартует в 16-битном режиме;
2. отключает прерывания;
3. загружает временную GDT;
4. включает protected mode;
5. переходит в 32-битный режим;
6. включает PAE;
7. загружает CR3;
8. включает long mode через EFER;
9. включает paging;
10. делает far jump в 64-битный код;
11. настраивает сегментные регистры;
12. создаёт отдельный стек для AP;
13. прыгает в kernel entry point.
```

Главная цель этого файла:

```text
привести AP-процессор к такому же режиму работы,
в котором уже находится BSP.
```

---

`processor_init.nasm` не знает заранее:

```text
какой CR3 нужно загрузить;
куда прыгать после перехода в long mode.
```

Поэтому BSP заранее записал эти значения в конец `processor_init`.

В конце `processor_init.nasm` есть:

```asm
pml4_address dq 0
entry_point dq 0
```

После патча BSP эти значения уже не нулевые:

```text
pml4_address = CR3 BSP
entry_point = 0x2000
```

Поэтому AP может сделать:

```text
1. загрузить pml4_address в CR3;
2. включить long mode;
3. прочитать entry_point;
4. перейти в kernel.
```

---

После перехода в 64-битный режим `processor_init.nasm` читает APIC ID текущего процессора.

Идея примерно такая:

```text
APIC ID текущего CPU читается из Local APIC;
APIC ID умножается на 4096;
получается смещение для стека;
стек ставится по адресу 0x100000 + APIC_ID * 4096.
```

Схема:

```text
APIC ID 0 → stack = 0x100000
APIC ID 1 → stack = 0x101000
APIC ID 2 → stack = 0x102000
APIC ID 3 → stack = 0x103000
```

Это нужно, чтобы несколько процессоров не использовали один и тот же стек.

Если бы все процессоры использовали один стек, они бы портили данные друг друга, и ядро быстро бы зависло или упало.

---

После выполнения `processor_init.nasm` AP уже:

```text
работает в 64-битном long mode;
использует те же таблицы страниц, что и BSP;
имеет свой отдельный стек;
знает адрес входа в kernel;
готов выполнять основной код ядра.
```

После этого он прыгает на адрес:

```text
0x2000
```

То есть туда, где находится `_entry` основного ядра.

---

# Этап 15. AP снова приходит в `entry.nasm`, но с `RAX = 0`

В конце `processor_init.nasm` AP переходит в kernel. Перед этим он специально обнуляет `RAX`:

```asm
xor rax, rax
jmp far [jmp_value]
```

`xor rax, rax` — это быстрый способ записать в `RAX` значение `0`.

То есть AP приходит в `_entry` с таким состоянием:

```text
RAX = 0
```

---

Когда BSP впервые пришёл в kernel из UEFI-загрузчика, в `RAX` лежал указатель на `SystemInfo`.

То есть для BSP было так:

```text
RAX = адрес SystemInfo
```

А `entry.nasm` сделал:

```asm
test rax, rax
jz .skip_setup

mov [rel info], rax
```

Смысл:

  

```text
если RAX не равен 0,
значит это первый вход от загрузчика;
надо сохранить указатель SystemInfo в глобальную переменную info.
```

BSP действительно приходит первым, поэтому он сохраняет:

```text
info = SystemInfo*
```

---

AP-процессор приходит в `_entry` позже. К этому моменту `info` уже установлен BSP-процессором.

Если бы AP тоже пытался заново записывать `info`, он мог бы записать туда неправильное значение.

Поэтому AP приходит с:

```text
RAX = 0
```

И в `entry.nasm` срабатывает переход:

```asm
test rax, rax
jz .skip_setup
```

Так как `RAX = 0`, команда `jz .skip_setup` выполняется. То есть AP пропускает блок:

```asm
mov [rel info], rax
```
И не трогает глобальную переменную `info`.

---

После перехода на `.skip_setup` AP всё равно продолжает выполнение общего входного кода.

Дальше он доходит до:

```asm
call main
```

То есть AP тоже вызывает `main()`. Получается:

```text
BSP:
LabExitBs → entry.nasm с RAX = SystemInfo* → main()

AP:
processor_init → entry.nasm с RAX = 0 → main()
```

Оба типа процессоров в итоге попадают в одну и ту же функцию `main()`. Разница только в том, что BSP до этого сохранил `info`, а AP просто пользуются уже готовым `info`.

---

Когда AP входит в `main()`, он выполняет тот же код, что и BSP. Но условие:

```c
if (get_self_apic_id() == 0)
startup_processors();
```

срабатывает только на процессоре с APIC ID 0. То есть только BSP запускает остальные процессоры. AP-процессоры не вызывают `startup_processors()` повторно. После этого каждый процессор получает свой APIC ID и рисует свою полосу на экране.

Условно:

```text
CPU с APIC ID 0 рисует строки 0–9;
CPU с APIC ID 1 рисует строки 10–19;
CPU с APIC ID 2 рисует строки 20–29;
CPU с APIC ID 3 рисует строки 30–39.
```

Именно поэтому после успешного запуска нескольких процессоров на экране появляются несколько полос.

### Этап 16. Все процессоры выполняют `main()` и рисуют полосы

После входа в `main()` каждый процессор читает свой APIC ID:

```c
u32 self_apic_id = get_self_apic_id();
```

И рисует свою горизонтальную область:

```c
for (u64 i = self_apic_id * 10; i < (self_apic_id + 1) * 10; ++i) {
  for (u64 j = 0; j < w; ++j) {
    b[i * w + j] = color;
  }
}
```

Если QEMU запущен с:

```bash
-smp 4
```

ожидаются 4 полосы:

```text
APIC ID 0 -> строки 0-9
APIC ID 1 -> строки 10-19
APIC ID 2 -> строки 20-29
APIC ID 3 -> строки 30-39
```

---

# 2. Что делает каждый файл

---

# 2.1. `MySuperPkg.dsc`

## Архитектурно

`MySuperPkg.dsc` — это файл платформы edk2.

Он говорит системе сборки edk2:

1. как называется платформа;
2. какие архитектуры поддерживаются;
3. какие библиотеки доступны модулям;
4. какие компоненты нужно собирать;
5. какие `.inf`-файлы входят в проект.

То есть edk2 должен знать, что надо собрать UEFI-приложение `LabExitBs.efi`.

## Где это в проекте

Примерно в `.dsc` должна быть секция:

```ini
[Components]
  MySuperPkg/LabExitBs/LabExitBs.inf
```

---

# 2.2. `MySuperPkg.dec`

## Архитектурно

`MySuperPkg.dec` — descriptor пакета edk2.

Он описывает пакет как единицу edk2:

1. имя пакета;
2. GUID пакета;
3. версию;
4. возможные include-директории;
5. GUID/Protocol/PCD, если пакет их объявляет.

`.dec` в основном нужен, чтобы пакет `MySuperPkg` был корректным edk2-пакетом.

---

# 2.3. `LabExitBs.inf`

## Архитектурно

`LabExitBs.inf` описывает edk2-модуль `LabExitBs`.

Он говорит edk2:

1. это UEFI-приложение;
2. его имя `LabExitBs`;
3. точка входа — `UefiMain`;
4. исходный файл — `main.c`;
5. какие пакеты и библиотеки нужны.

## Пошагово файл делает следующее на этапе сборки

1. объявляет модуль как `UEFI_APPLICATION`;
2. задаёт `BASE_NAME = LabExitBs`;
3. задаёт `ENTRY_POINT = UefiMain`;
4. подключает `main.c` как исходник;
5. подключает `MdePkg` и `MdeModulePkg`;
6. подключает библиотеки для UEFI-приложения: `UefiLib`, `PrintLib`, `UefiBootServicesTableLib`, `MemoryAllocationLib`, `BaseLib` и другие.

`.inf` — это рецепт сборки одного UEFI-модуля.

---

# 2.4. `LabExitBs/main.c`

Это главный UEFI-загрузчик.

Именно он превращает UEFI-приложение в загрузчик нашего собственного ядра.

## Архитектурно `LabExitBs/main.c` делает следующее

1. подключает UEFI-заголовки и `kernel.inc`;
2. описывает структуру `SystemInfo`, которую поймёт ядро;
3. создаёт глобальную переменную `static SystemInfo info`;
4. получает `EFI_MP_SERVICES_PROTOCOL`;
5. получает количество процессоров;
6. получает `ProcessorId` каждого процессора;
7. получает `Graphics Output Protocol`;
8. читает framebuffer address, width, height;
9. получает карту памяти UEFI;
10. вызывает `ExitBootServices`;
11. записывает video-информацию в `info`;
12. копирует встроенный `kernel[]` в память по адресу `0x2000`;
13. кладёт `&info` в `RAX`;
14. прыгает на `0x2000`.

## 2.4.1. Подключение `kernel.inc`

В начале файла:

```c
#include "kernel.inc"
```

Это означает, что бинарник kernel встроен прямо в `LabExitBs.efi` как массив байтов.

`kernel.inc` создаётся командой:

```bash
xxd -i kernel > kernel.inc
```

## 2.4.2. Структуры данных

В загрузчике и ядре структуры должны совпадать:

```c
typedef struct _VideoBufferInfo {
  u64 buffer;
  u64 height;
  u64 width;
} __attribute__((packed)) VideoBufferInfo;

typedef struct _ProcessorsInfo {
  u8 size;
  u8 ids[256];
} __attribute__((packed)) ProcessorsInfo;

typedef struct _SystemInfo {
  VideoBufferInfo video;
  ProcessorsInfo processors;
} __attribute__((packed)) SystemInfo;
```

## 2.4.3. Работа с MP Services

Функция:

```c
BOOLEAN context_setup_mp(Context *self)
```

делает:

```c
s = gBS->LocateProtocol(&mp_guid, NULL, (void **)&mp);
```

То есть ищет `EFI_MP_SERVICES_PROTOCOL`.

Потом:

```c
UINTN context_processors_count(Context *self)
```

вызывает:

```c
self->mp->GetNumberOfProcessors(self->mp, &count, &enabled);
```

А:

```c
EFI_PROCESSOR_INFORMATION context_get_processor_info(Context *self, UINTN index)
```

вызывает:

```c
self->mp->GetProcessorInfo(self->mp, index, &info);
```

В `UefiMain` это используется так:

```c
UINTN proc_count = context_processors_count(&ctx);
info.processors.size = proc_count;

for (UINTN i = 0; i < proc_count; ++i) {
  EFI_PROCESSOR_INFORMATION efi_proc_info =
      context_get_processor_info(&ctx, i);

  info.processors.ids[i] = efi_proc_info.ProcessorId;
}
```

Так ядро потом узнает, какие APIC ID запускать.

## 2.4.4. Работа с GOP

Функция:

```c
EFI_STATUS GetGop(EFI_GRAPHICS_OUTPUT_PROTOCOL **Gop)
```

делает:

1. `LocateHandleBuffer` по протоколу `gEfiGraphicsOutputProtocolGuid`;
2. перебирает найденные handles;
3. вызывает `HandleProtocol`;
4. возвращает указатель на GOP.

В `UefiMain`:

```c
Status = GetGop(&Gop);

VideoBufferAddress = Gop->Mode->FrameBufferBase;
HorisontalRes = Gop->Mode->Info->HorizontalResolution;
VerticalRes = Gop->Mode->Info->VerticalResolution;
```

После этого загрузчик знает, куда ядро должно писать пиксели.

## 2.4.5. Получение карты памяти

В `UefiMain`:

```c
Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey,
                           &DescriptorSize, &DescriptorVersion);
```

Если буфер мал, выделяется память:

```c
Status = gBS->AllocatePool(EfiBootServicesData,
                           MemoryMapSize + 5 * DescriptorSize,
                           (void **)&MemoryMap);
```

Потом `GetMemoryMap` вызывается снова.

`MapKey` нужен для `ExitBootServices`.

## 2.4.6. Выход из Boot Services

```c
Status = gBS->ExitBootServices(ImageHandle, MapKey);
```

После этого `gBS` использовать уже нельзя.

## 2.4.7. Копирование kernel в `0x2000`

```c
unsigned char *dst = (void *)0x2000;
for (unsigned int i = 0; i < kernel_len; ++i) {
  dst[i] = kernel[i];
}
```

Здесь `kernel` и `kernel_len` пришли из `kernel.inc`.

## 2.4.8. Передача управления в ядро

```c
__asm__ __volatile__("mov $0x2000, %%rdi\n\t"
                     "mov %0, %%rax\n\t"
                     "jmp %%rdi\n\t"
                     :
                     : "r"(&info)
                     :);
```

Это означает:

1. `RDI = 0x2000`;
2. `RAX = &info`;
3. `jmp RDI`.

В результате управление уходит в `entry.nasm`.

---

# 2.5. `kernel.inc`

## Архитектурно

`kernel.inc` — это бинарник `kernel`, превращённый в C-массив.

Он нужен, чтобы `LabExitBs.efi` не читал kernel с диска, а уже содержал его внутри себя.

## Пошагово

1. Сначала собирается `mini-kernel/kernel`.
2. Потом выполняется:

```bash
xxd -i kernel > kernel.inc
```

3. В `kernel.inc` появляется:

```c
unsigned char kernel[] = { ... };
unsigned int kernel_len = ...;
```

4. `LabExitBs/main.c` подключает файл:

```c
#include "kernel.inc"
```

5. Загрузчик копирует этот массив в память по адресу `0x2000`.

## Важный технический момент

Каждый раз, когда ты меняешь код ядра и заново делаешь `make`, нужно заново генерировать `kernel.inc`.

Иначе `LabExitBs.efi` будет содержать старую версию kernel.

Правильный цикл:

```bash
cd ~/edk2/MySuperPkg/mini-kernel
make
xxd -i kernel > kernel.inc
cp kernel.inc ~/edk2/MySuperPkg/LabExitBs/kernel.inc

cd ~/edk2
source edksetup.sh
build -a X64 -t GCC5 -p MySuperPkg/MySuperPkg.dsc
```

---

# 2.6. `mini-kernel/Makefile`

## Архитектурно

`Makefile` собирает сам mini-kernel.

Он не собирает UEFI-приложение. Он собирает bare-metal бинарник `kernel`.

## Пошагово Makefile делает

1. собирает `processor_init.nasm` в бинарник `processor_init`;
2. превращает `processor_init` в `processor_init.inc`;
3. собирает `entry.nasm` в `entry.o`;
4. компилирует C-файлы и линкует их с `entry.o`;
5. создаёт `kernel.elf`;
6. с помощью `objcopy` превращает `kernel.elf` в сырой бинарник `kernel`.

## Где это в Makefile

```makefile
entry.o: entry.nasm
	nasm -f elf64 -o entry.o entry.nasm
```

Сборка входного asm-файла.

```makefile
processor_init: processor_init.nasm
	nasm -f bin processor_init.nasm
```

Сборка trampoline-кода как сырого бинарника.

```makefile
processor_init.inc: processor_init
	xxd -i processor_init > processor_init.inc
```

Упаковка trampoline-кода в C-массив.

```makefile
kernel.elf: ${SOURCES} entry.o processor_init.inc
	${CC} ${CFLAGS} entry.o -o kernel.elf ${SOURCES}
```

Линковка ядра.

```makefile
kernel: kernel.elf
	objcopy -O binary kernel.elf kernel
```

Создание сырого бинарника.

## Технические моменты в CFLAGS

В `CFLAGS` есть важные опции:

1. `-nostdlib` — не использовать стандартную библиотеку C.
2. `-m64` — собирать под x86-64.
3. `-mno-red-zone` — отключить red zone, важно для kernel/interrupt-кода.
4. `-Wl,--entry,_entry` — точка входа `_entry`.
5. `-Wl,--script=linker.lds` — использовать свой linker script.
6. `-fno-builtin` — не заменять вызовы на встроенные реализации компилятора.
7. `-ffunction-sections -fdata-sections` и `--gc-sections` — раскладывать функции/данные по секциям и выкидывать неиспользуемое.

---

# 2.7. `mini-kernel/linker.lds`

## Архитектурно

`linker.lds` говорит линковщику, как расположить код и данные внутри `kernel.elf`.

Ключевая задача: секция `.entry` должна оказаться в начале.

## Пошагово файл делает

1. создаёт один `PT_LOAD` сегмент;
2. размещает `.entry` первой внутри `.text`;
3. размещает обычный `.text`;
4. размещает `.rodata`;
5. размещает `.data` и `.bss`;
6. выбрасывает ненужные ELF-секции.

## Где это в коде

```lds
.text : ALIGN(CONSTANT(COMMONPAGESIZE)) {
  KEEP(*(.entry))
  *(.text .text.*)
  *(.rodata .rodata.*)
} : load
```

`KEEP(*(.entry))` — очень важно.

Оно гарантирует, что `_entry` из `entry.nasm` не будет выкинут и окажется в начале секции.

Дальше:

```lds
.data ALIGN(ALIGNOF(.text)) : ALIGN(CONSTANT(COMMONPAGESIZE)) {
  *(.data .data.*)
  *(.bss .bss.*)
} : load
```

Тут размещаются глобальные переменные, например:

```c
SystemInfo *info;
```

И блок:

```lds
/DISCARD/ : {
  *(.note.GNU-stack)
  *(.interp)
  *(.dynamic)
  ...
}
```

выбрасывает ненужные для нашего bare-metal binary секции.

---

# 2.8. `mini-kernel/entry.nasm`

## Архитектурно

`entry.nasm` — первая точка входа в kernel.

В него попадают два типа процессоров:

1. BSP — главный процессор, пришедший от `LabExitBs.efi`.
2. AP — дополнительные процессоры, пришедшие после `processor_init.nasm`.

## Пошагово `entry.nasm` делает

1. стартует в 64-битном режиме;
2. проверяет, есть ли в `RAX` указатель на `SystemInfo`;
3. если `RAX != 0`, сохраняет его в глобальную переменную `info`;
4. если это первый вход от BSP, выставляет стек `RSP = 0x100000`;
5. вызывает `main()`;
6. если `main()` вернулся, зависает в бесконечном цикле.

## Привязка к коду

### 1. Объявление точки входа

```asm
global _entry

extern main
extern info;

section .entry
[bits 64]
_entry:
```

`global _entry` делает `_entry` видимым для линковщика.

`extern main` означает: функция `main` находится в C-коде.

`extern info` означает: глобальная переменная `info` находится в C-коде.

### 2. Проверка `RAX`

```asm
test rax, rax
jz .skip_setup
```

Если `RAX == 0`, значит это AP-процессор, он пришёл не от загрузчика, а после trampoline-кода.

Если `RAX != 0`, значит это BSP, пришедший от `LabExitBs.efi`.

### 3. Сохранение `SystemInfo`

```asm
mov [rel info], rax
```

Здесь адрес структуры `SystemInfo` записывается в глобальную переменную ядра:

```c
SystemInfo *info;
```

### 4. Настройка стека BSP

```asm
mov rsp, 0x100000
mov rbp, rsp
```

BSP получает стек по адресу `0x100000`.

### 5. Переход в `main()`

```asm
.to_main:
  call main
```

После этого выполнение переходит в C-код ядра.

### 6. Бесконечная остановка

```asm
jmp $
```

Если `main()` вернётся, процессор зависнет на этой инструкции.

---

# 2.9. `mini-kernel/main.c`

Это основной код ядра.

## Архитектурно `main.c` делает

1. подключает `processor_init.inc`;
2. описывает структуры `SystemInfo`, `VideoBufferInfo`, `ProcessorsInfo`;
3. хранит глобальный указатель `SystemInfo *info`;
4. умеет очищать экран;
5. умеет читать APIC ID текущего процессора;
6. умеет запускать AP-процессор через Local APIC;
7. копирует trampoline-код в `0x1000`;
8. патчит в trampoline-код текущий `CR3`;
9. патчит в trampoline-код entry point ядра `0x2000`;
10. запускает все AP из `info->processors.ids`;
11. каждый процессор рисует свою полосу на экране.

## 2.9.1. Подключение `processor_init.inc`

```c
#include "processor_init.inc"
```

Это подключает массив:

```c
unsigned char processor_init[] = { ... };
unsigned int processor_init_len = ...;
```

Он нужен для запуска AP.

## 2.9.2. Структуры данных

```c
typedef struct _SystemInfo {
  VideoBufferInfo video;
  ProcessorsInfo processors;
} __attribute__((packed)) SystemInfo;

SystemInfo *info;
```

`info` заполняется в `entry.nasm`.

## 2.9.3. `screen_clear`

```c
void screen_clear(VideoBufferInfo *video, u32 color) {
  u64 h = info->video.height;
  u64 w = info->video.width;
  u32 *b = (u32 *)info->video.buffer;

  for (u64 i = 0; i < h; ++i) {
    for (u64 j = 0; j < w; ++j) {
      b[i * w + j] = color;
    }
  }
}
```

Функция очищает framebuffer.

Технический нюанс: параметр `video` передаётся, но внутри не используется. Функция берёт данные из глобального `info`.

## 2.9.4. `get_self_apic_id`

```c
u8 get_self_apic_id(void) {
  u32 apic_id;
  __asm volatile("movl 0xfee00020, %0" : "=r"(apic_id)::);
  return apic_id >> 24;
}
```

Адрес `0xFEE00020` — Local APIC ID Register.

Старшие 8 бит содержат APIC ID процессора.

Поэтому делается:

```c
return apic_id >> 24;
```

## 2.9.5. `start_ap`

```c
void start_ap(u8 ap_apic_id, u32 init_code_entry)
```

Эта функция будит один AP-процессор.

В начале задаются адреса регистров Local APIC:

```c
volatile u32 *const APIC_ICR_LOW = (void *)0xfee00300;
volatile u32 *const APIC_ICR_HIG = (void *)0xfee00310;
```

`ICR` — Interrupt Command Register.

Через него BSP отправляет команды другим процессорам.

### INIT

```c
*APIC_ICR_HIG = (u32)ap_apic_id << 24;
*APIC_ICR_LOW = 0x00004500;
```

Это INIT IPI.

Он переводит AP в начальное состояние.

### Ожидание доставки

```c
while (*APIC_ICR_LOW & (1 << 12))
  ;
```

Бит 12 — Delivery Status.

Пока он установлен, команда ещё доставляется.

### SIPI

```c
*APIC_ICR_HIG = (u32)ap_apic_id << 24;
*APIC_ICR_LOW = ((u32)0x00004600 | (init_code_entry >> 12));
```

Это STARTUP IPI.

`init_code_entry >> 12` превращает адрес `0x1000` в vector `1`.

AP стартует по адресу:

```text
vector * 0x1000 = 1 * 0x1000 = 0x1000
```

## 2.9.6. `startup_processors`

```c
void startup_processors(void)
```

Это функция, которую вызывает BSP.

### 1. Очистка экрана

```c
screen_clear(&info->video, 0);
```

### 2. Копирование trampoline-кода

```c
for (u32 i = 0; i < processor_init_len; ++i) {
  ((volatile u8 *)0x1000)[i] = processor_init[i];
}
```

Теперь по адресу `0x1000` лежит код из `processor_init.nasm`.

### 3. Чтение `CR3`

```c
u64 cr3 = 0;
__asm volatile("movq %%cr3, %0" : "=r"(cr3) : :);
```

`CR3` содержит адрес PML4 — верхнего уровня таблиц страниц.

AP должен использовать те же таблицы страниц, что и BSP.

### 4. Патчинг `pml4_address`

```c
*(volatile u64 *)((u8 *)0x1000 + processor_init_len - 16) = cr3;
```

В конце `processor_init.nasm` есть:

```asm
pml4_address dq 0
entry_point  dq 0
```

`pml4_address` находится за 16 байт до конца бинарника.

### 5. Патчинг `entry_point`

```c
*(volatile u64 *)((u8 *)0x1000 + processor_init_len - 8) = 0x2000;
```

`entry_point` находится за 8 байт до конца бинарника.

AP после перехода в long mode прыгнет в `0x2000`.

### 6. Запуск всех AP

```c
u8 count = info->processors.size;

for (u8 i = 1; i < count; ++i) {
  u8 ap_apic_id = info->processors.ids[i];
  start_ap(ap_apic_id, 0x1000);
}
```

Индекс `0` пропускается, потому что это обычно BSP.

## 2.9.7. `main`

```c
int main() {
  if (get_self_apic_id() == 0)
    startup_processors();
```

Если текущий процессор — BSP, он запускает остальные.

Дальше каждый процессор рисует свою полосу:

```c
u32 self_apic_id = get_self_apic_id();

for (u32 color = 0;; color = ((color + 1) & 0x00ffffff)) {
  for (u64 i = self_apic_id * 10; i < (self_apic_id + 1) * 10; ++i) {
    for (u64 j = 0; j < w; ++j) {
      b[i * w + j] = color;
    }
  }
}
```

---

# 2.10. `mini-kernel/processor_init.nasm`

Это самый низкоуровневый файл.

Он выполняется не на BSP, а на AP-процессорах после SIPI.

## Архитектурно `processor_init.nasm` делает

1. стартует в 16-битном режиме;
2. обнуляет `DS`;
3. отключает прерывания;
4. загружает временную GDT;
5. включает protected mode через `CR0.PE`;
6. делает far jump в 32-битный код;
7. настраивает 32-битный data segment;
8. включает PAE через `CR4.PAE`;
9. загружает `CR3` адресом PML4;
10. включает long mode через `EFER.LME`;
11. включает paging через `CR0.PG`;
12. делает far jump в 64-битный code segment;
13. настраивает 64-битные сегменты данных;
14. читает свой APIC ID;
15. вычисляет отдельный стек по формуле `0x100000 + APIC_ID * 4096`;
16. записывает адрес ядра в far pointer;
17. обнуляет `RAX`;
18. прыгает в kernel entry point.

Теперь по коду.

## 2.10.1. Код должен лежать по адресу `0x1000`

```asm
org 1000h
use16
```

`org 1000h` говорит ассемблеру: считай, что этот код будет загружен по адресу `0x1000`.

`use16` означает: дальше код собирается как 16-битный.

## 2.10.2. Старт в 16-битном режиме

```asm
START:
    xor ax, ax
    mov ds, ax

    cli
```

Здесь:

1. `AX = 0`;
2. `DS = 0`;
3. `cli` отключает прерывания.

## 2.10.3. Загрузка временной GDT

```asm
lgdt [tmp_gdtr]
```

GDT нужна, чтобы перейти в protected mode и long mode.

## 2.10.4. Включение protected mode

```asm
mov eax, cr0
or al, 01h
mov cr0, eax
```

Здесь устанавливается бит `PE` в `CR0`.

`PE` = Protection Enable.

После этого нужен far jump:

```asm
jmp 8h:PROTECTED_MODE_ENTRY_POINT
```

`8h` — селектор 32-битного code segment в GDT.

## 2.10.5. Временная GDT

```asm
tmp_gdt:
    db  00h,  00h, 00h, 00h, 00h,       00h,       00h, 00h
    db 0FFh, 0FFh, 00h, 00h, 00h, 10011010b, 11001111b, 00h  ; сегмент кода 32
    db 0FFh, 0FFh, 00h, 00h, 00h, 10010010b, 11001111b, 00h  ; сегмент данных
    db  00h,  00h, 00h, 00h, 00h,       9Ah,       20h, 00h  ; сегмент кода 64
    db  00h,  00h, 00h, 00h, 00h,       92h,       00h, 00h  ; сегмент данных 64
```

Селекторы:

```text
0x00 -> null descriptor
0x08 -> 32-bit code segment
0x10 -> 32-bit data segment
0x18 -> 64-bit code segment
0x20 -> 64-bit data segment
```

## 2.10.6. Переход в 32-битный protected mode

```asm
use32
PROTECTED_MODE_ENTRY_POINT:
    mov ax, 10h
    mov ds, ax
```

Здесь уже выполняется 32-битный код.

`0x10` — селектор data segment.

## 2.10.7. Включение PAE

```asm
mov eax, cr4
bts eax, 5
mov cr4, eax
```

Бит 5 в `CR4` — это `PAE`.

Для long mode нужен PAE.

## 2.10.8. Загрузка CR3

```asm
mov eax, [pml4_address]
mov cr3, eax
```

`pml4_address` был заранее пропатчен в `main.c` ядра:

```c
*(volatile u64 *)((u8 *)0x1000 + processor_init_len - 16) = cr3;
```

`CR3` теперь указывает на PML4.

## 2.10.9. Включение Long Mode Enable

```asm
mov ecx, 0C0000080h
rdmsr
bts eax, 8
wrmsr
```

`0xC0000080` — MSR-регистр `EFER`.

Бит 8 — `LME`, Long Mode Enable.

## 2.10.10. Включение paging

```asm
mov eax, cr0
bts eax, 31
mov cr0, eax
```

Бит 31 в `CR0` — `PG`, paging.

Когда включены:

1. `CR4.PAE`;
2. `EFER.LME`;
3. `CR0.PG`;

процессор может войти в 64-битный long mode.

## 2.10.11. Far jump в 64-битный код

```asm
jmp 18h:LONG_MODE_ENTRY_POINT
```

`0x18` — селектор 64-битного code segment.

## 2.10.12. Настройка сегментов в long mode

```asm
use64
LONG_MODE_ENTRY_POINT:
    mov ax, 20h
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
```

`0x20` — 64-битный data segment.

## 2.10.13. Получение APIC ID текущего AP

```asm
mov rsi, 0xfee00020
mov eax, [rsi]
shr eax, 24
```

Адрес `0xFEE00020` — Local APIC ID Register.

Сдвиг на 24 бита оставляет APIC ID.

## 2.10.14. Настройка отдельного стека

```asm
shl eax, 12
lea rsp, [0x100000 + eax]
mov rbp, rsp
```

`shl eax, 12` означает умножение APIC ID на 4096.

Формула:

```text
stack = 0x100000 + APIC_ID * 4096
```

Пример:

```text
APIC ID 0 -> 0x100000
APIC ID 1 -> 0x101000
APIC ID 2 -> 0x102000
APIC ID 3 -> 0x103000
```

## 2.10.15. Подготовка far jump в kernel

```asm
mov rbx, [entry_point]
mov [jmp_value.address], rbx
```

`entry_point` был пропатчен в `main.c` ядра:

```c
*(volatile u64 *)((u8 *)0x1000 + processor_init_len - 8) = 0x2000;
```

## 2.10.16. Обнуление RAX и прыжок

```asm
xor rax, rax
jmp far [jmp_value]
```

`RAX = 0`, чтобы `entry.nasm` понял: это AP, а не первый вход от загрузчика.

---

# 2.11. `processor_init.inc`

## Архитектурно

`processor_init.inc` — это `processor_init.nasm`, превращённый в C-массив.

## Пошагово

1. `processor_init.nasm` собирается в бинарник:

```bash
nasm -f bin processor_init.nasm
```

2. Потом создаётся include-файл:

```bash
xxd -i processor_init > processor_init.inc
```

3. В `main.c` ядра он подключается:

```c
#include "processor_init.inc"
```

4. В `startup_processors()` байты копируются в `0x1000`.

---

# 2.12. `processor_init`

## Архитектурно

`processor_init` — это уже готовый бинарный trampoline-код.

Его нельзя запускать как приложение. Это просто набор машинных инструкций, который BSP копирует по адресу `0x1000`.

---

# 2.13. `kernel.elf`

## Архитектурно

`kernel.elf` — промежуточный ELF-файл.

Он нужен для линковки, но непосредственно в `LabExitBs` мы встраиваем не его.

Из него потом делают сырой бинарник `kernel`.

---

# 2.14. `kernel`

## Архитектурно

`kernel` — это сырой бинарник mini-kernel.

Именно он потом превращается в `kernel.inc`.

---

## 3. Ключевые технические моменты

### 3.1. UEFI-приложение и kernel — это разные вещи

`LabExitBs.efi` — UEFI-приложение. Его может запустить UEFI Shell.

`kernel` — сырой бинарник. Его нельзя запустить напрямую из Shell.

---

### 3.2. Почему нужен `ExitBootServices`

Пока работают Boot Services, система ещё находится под управлением UEFI.

После `ExitBootServices` управление переходит нашему коду.

Это граница между:

```text
UEFI application
```

и:

```text
собственное ядро / bare-metal код
```

---

### 3.3. Почему адрес `0x2000`

Kernel копируется в `0x2000`, потому что:

1. `LabExitBs` прыгает на `0x2000`;
2. AP-процессорам в `processor_init` патчится `entry_point = 0x2000`;
3. `entry.nasm` должен быть в начале бинарника kernel.

---

### 3.4. Почему адрес `0x1000`

`processor_init` копируется в `0x1000`, потому что AP стартует после SIPI по формуле:

```text
SIPI vector * 0x1000
```

При vector `1` получается `0x1000`.

---

### 3.5. Почему `SystemInfo` передаётся через `RAX`

Потому что `entry.nasm` явно читает `RAX`:

```asm
test rax, rax
mov [rel info], rax
```

Обычный C-вызов не подходит, потому что edk2 использует свою calling convention, и аргумент не гарантированно окажется в `RAX`.

---

### 3.6. Что такое BSP и AP

BSP — Bootstrap Processor. Это главный процессор, который первым начал выполнение.

AP — Application Processor. Это дополнительные процессоры, которые BSP запускает вручную.

В коде предполагается:

```c
if (get_self_apic_id() == 0)
  startup_processors();
```

То есть APIC ID `0` считается BSP.

---

### 3.7. Что такое Local APIC

Local APIC — контроллер прерываний внутри каждого процессора.

В коде используются MMIO-адреса:

```text
0xFEE00020 -> Local APIC ID Register
0xFEE00300 -> ICR Low
0xFEE00310 -> ICR High
```

Через ICR BSP отправляет INIT/SIPI другим CPU.

---

### 3.8. Что такое INIT/SIPI

INIT IPI сбрасывает AP-процессор в начальное состояние.

SIPI говорит AP, с какого адреса начать выполнение.

В нашем коде AP начинает с `0x1000`, где лежит `processor_init`.

---

### 3.9. Что такое CR3

`CR3` содержит физический адрес PML4 — верхнего уровня таблиц страниц x86-64.

AP должен использовать те же таблицы страниц, что и BSP, поэтому BSP читает свой `CR3` и записывает его в trampoline-код.

---

### 3.10. Что такое PAE, EFER.LME и CR0.PG

Чтобы включить 64-битный long mode, AP должен включить:

1. `CR4.PAE`;
2. `EFER.LME`;
3. `CR0.PG`.

После этого far jump в 64-битный code segment завершает переход в long mode.

---

### 3.11. Почему у каждого CPU отдельный стек

Если все процессоры будут использовать один стек, они перетрут данные друг друга.

Поэтому `processor_init.nasm` делает:

```text
stack = 0x100000 + APIC_ID * 4096
```

---

## 4. Итоговая схема файлов

```text
MySuperPkg/
├── MySuperPkg.dsc
├── MySuperPkg.dec
│
├── LabExitBs/
│   ├── LabExitBs.inf
│   ├── main.c
│   └── kernel.inc
│
├── mini-kernel/
│   ├── Makefile
│   ├── linker.lds
│   ├── entry.nasm
│   ├── main.c
│   ├── processor_init.nasm
│   ├── processor_init
│   ├── processor_init.inc
│   ├── entry.o
│   ├── kernel.elf
│   └── kernel
```

---

## 5. Практический порядок пересборки проекта

Когда меняешь код mini-kernel:

```bash
cd ~/edk2/MySuperPkg/mini-kernel
make
```

Потом обновляешь `kernel.inc`:

```bash
xxd -i kernel > kernel.inc
cp kernel.inc ~/edk2/MySuperPkg/LabExitBs/kernel.inc
```

Потом пересобираешь edk2-загрузчик:

```bash
cd ~/edk2
source edksetup.sh
build -a X64 -t GCC5 -p MySuperPkg/MySuperPkg.dsc
```

Потом копируешь `LabExitBs.efi` как `BOOTX64.EFI`:

```bash
cp путь/до/LabExitBs.efi /home/max/uefi-bootloader/uefi_share/EFI/BOOT/BOOTX64.EFI
```

Потом запускаешь QEMU.

---
