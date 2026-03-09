#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

/*
UEFI возвращает много типов памяти, но мы глобально делаим их на две категории: 
1) доступная нам 
2) недоступная нам
*/
typedef enum {
  MmapEntryType_NotAvailable,         // Считаем участок памяти недоступным
  MmapEntryType_Available             // Считаем участок памяти доступным
} MmapEntryType;

/*
Структура, описывающая один участок памяти в нашей собственной карте памяти
Мы берем стандартный дескриптор UEFI и превращаем его в более простой формат:
addr + size + type 
*/
typedef struct _MmapEntry {
  UINT64 addr;                        // Начальный физический адрес участка памяти
  UINT64 size;                        // Размер участка памяти в байтах
  UINT8  type;                        // Наш тип: доступен / недоступен
} MmapEntry;

/*
Структура, описывающая всю нашу карту памяти в целом
*/
typedef struct _Mmap {
  UINT64 size;                        // Количество записей в карте памяти (сколько участков памяти содержится в массиве entries[])
  MmapEntry entries[];                // Массив записей, хранящий все участки памяти один за другим  
} Mmap;

/*
Функция получения карты памяти UEFI:
1) Узнаем новый размер буфера
2) Если буфер мал - увеличиваем его
3) Повторяем это в цикле, пока не получим карту памяти успешно
*/ 
EFI_STATUS
GetUefiMemoryMap(
  EFI_MEMORY_DESCRIPTOR **OutMemoryMap,
  UINTN *OutMemoryMapSize,
  UINTN *OutMapKey,
  UINTN *OutDescriptorSize,
  UINT32 *OutDescriptorVersion
)
{
  EFI_STATUS s;                       // Храним результаты вызовов функций UEFI (EFI_SUCCESS, EFI_BUFFER_TOO_SMALL)
  EFI_MEMORY_DESCRIPTOR *mmap = NULL; // Указатель на буфер куда UEFI запишет карту памяти, NULL потому что память ешё не выделена
  UINTN mmap_size = 0;                // Размер буфера под карту памяти
  UINTN mmap_key = 0;                 // Ключ карты памяти, важен для ExitBootServices()
  UINTN dscr_size = 0;                // Размер одного дескриптора
  UINT32 dscr_vers = 0;               // Версия структуры дискриптора памяти

  /*
  Бесконечный цикл получения карты памяти. Мы из него выходим когда:
  1) Успешно получили карту памяти
  2) Получили ошибку отличную от EFI_BUFFER_TOO_SMALL 
  */
  while (1) {

    /*
    Пытаемся получить карту памяти.
    При первом вызове mmap = NULL и mmap_size = 0
    Это означает, что пока не дается буфер а просто говорится какого размера он нужен
    В этом случае UEFI возвращает EFI_BUFFER_TOO_SMALL
    а в mmap_size записывается нужный размер буфера 
    */
    s = gBS->GetMemoryMap(
              &mmap_size,
              mmap,
              &mmap_key,
              &dscr_size,
              &dscr_vers
            );
    
    /*
    Если вернулся EFI_SUCCESS, значит карта памяти успешно получена
    */
    if (s == EFI_SUCCESS) {
      *OutMemoryMap = mmap;               // Возвращаем указатель на буфер с картой памяти
      *OutMemoryMapSize = mmap_size;      // Общий размер карты памяти в байтах
      *OutMapKey = mmap_key;              // Ключ карты памяти
      *OutDescriptorSize = dscr_size;     // Размер одного дескриптора
      *OutDescriptorVersion = dscr_vers;  // Версия дескриптора 
      return EFI_SUCCESS;                 // Завершаем функцию с успехом
    }

    /*
    Какая-то другая ошибка не EFI_BUFFER_TOO_SMALL
    Это не ожидаемая ситуация, а действительная ошибка
    */
    if (s != EFI_BUFFER_TOO_SMALL) { 
      if (mmap != NULL) {
        gBS->FreePool(mmap);              // Если до этого буфер выделялся, то освобождаем его,
      }                                   // чтобы не было утечек памяти
      return s;
    }

    /*
    Если мы дошли сюда, значит у нас s == EFI_BUFFER_TOO_SMALL
    Буфер слишком маленький его нужно выделить/увеличить
    */
    if (mmap != NULL) {                   // Если буфер уже был выделен на прошлой итерации
      gBS->FreePool(mmap);                // освобождаем его и будем выделять заново
      mmap = NULL;                        // Карта памяти могла увеличиться между вызовами и
    }                                     // требуемый размер мог увеличиться 

    // Добавляем небольшой запас, потому что карта памяти
    // может измениться между вызовами
    mmap_size += 2 * dscr_size;

    /* 
    Выделяем буфер под карту памяти
    EfiBootServicesData — тип памяти,
    mmap_size — сколько байт нужно,
    &mmap — сюда записывается адрес выделенной памяти.
    */
    s = gBS->AllocatePool(EfiBootServicesData, mmap_size, (VOID **)&mmap);
    if (EFI_ERROR(s)) {
      return s;
    }
  }
}

/*
Функция для преобразование стандартной карты памяти UEFI 
в нашу собственную карту памяти
*/
EFI_STATUS
ConvertMemoryMap(
  EFI_MEMORY_DESCRIPTOR *mmap,
  UINTN mmap_size,
  UINTN dscr_size,
  Mmap **OutNewMap
)
{
  EFI_STATUS s;
  UINT64 entries_count = mmap_size / dscr_size;                              // Вычисляем количество записей в карте памяти
  UINT64 new_mmap_size = sizeof(UINT64) + entries_count * sizeof(MmapEntry); // Вычисляем сколько памяти нужно под нашу новую карту памяти
  Mmap *new_mmap = NULL;                                                     // Указатель на новую карту памяти

  // Выделяем память под нашу новую карту памяти
  s = gBS->AllocatePool(EfiBootServicesData, new_mmap_size, (VOID **)&new_mmap);
  if (EFI_ERROR(s)) {
    return s;
  }

  // Записываем сколько записей будет в новой карте памяти
  new_mmap->size = entries_count;

  // Проходим по всем дескрипторам исходной карты памяти UEFI 
  for (UINT64 i = 0; i < new_mmap->size; ++i) {
    UINTN offset = dscr_size * i;                              // Вычисляем смещение до i-ого дескриптора в байтах

    /*
    Получаем указатель на текущий дескриптор памяти
    Здесь mmap приводится к UINT8*
    чтобы можно было двигаться по памяти на число байт.
    Потом результат снова приводится к EFI_MEMORY_DESCRIPTOR*.
    */
    EFI_MEMORY_DESCRIPTOR *d =
        (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mmap + offset);

    MmapEntryType t = MmapEntryType_NotAvailable;               // Считаем по умолчанию, что память не доступна

    switch (d->Type) {
      // Смотрим на тип памяти котоый вернул UEFI
      // Эти типы считаем доступными
      case EfiLoaderCode:
      case EfiLoaderData:
      case EfiBootServicesCode:
      case EfiBootServicesData:
      case EfiConventionalMemory:
        t = MmapEntryType_Available;
        break;

      default:
        t = MmapEntryType_NotAvailable;
        break;
    }

    new_mmap->entries[i].addr = d->PhysicalStart;             // Начальный физ адрес участка памяти
    new_mmap->entries[i].size = d->NumberOfPages * SIZE_4KB;  // Размер участка в байтах (число страниц * 4096)
    new_mmap->entries[i].type = t;                            // Наш тип (доступен/нелоступен)
  }

  *OutNewMap = new_mmap;                                      // Возвращаем наружу указатель на новую перепакованную карту памяти
  return EFI_SUCCESS;
}

/**
  Вывести перепакованную карту памяти.
**/
VOID
PrintMemoryMap(
  Mmap *mmap
)
{
  Print(L"Memory map entries count: %lu\r\n", mmap->size);                      
  Print(L"---------------------------------------------------------\r\n");
  Print(L"StartAddr         Size              Type\r\n");
  Print(L"---------------------------------------------------------\r\n");

  // Проходим по всем нашим записям карты памяти
  for (UINT64 i = 0; i < mmap->size; ++i) {
    Print(
      L"%016lx %016lx %d\r\n", 
      mmap->entries[i].addr,          // Начальный адрес участка памяти
      mmap->entries[i].size,          // Размер учатка памяти
      mmap->entries[i].type           // Тип (0 = недоступен, 1 = доступен)
    );
  }
}


/* 
Точка входа в UEFI - приложение 
*/
EFI_STATUS
EFIAPI
UefiMain(
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
)
{
  EFI_STATUS s;

  EFI_MEMORY_DESCRIPTOR *mmap = NULL;
  UINTN mmap_size = 0;
  UINTN mmap_key = 0;
  UINTN dscr_size = 0;
  UINT32 dscr_vers = 0;

  Mmap *new_mmap = NULL;                // Наша перепакованная карта памяти 

  // Получаем карту памяти UEFI в цикле
  s = GetUefiMemoryMap(
        &mmap,
        &mmap_size,
        &mmap_key,
        &dscr_size,
        &dscr_vers
      );

  // Если не удалось получить карту памяти выводим ошибку
  if (EFI_ERROR(s)) {
    Print(L"GetUefiMemoryMap error: %r\r\n", s);
    return s;
  }

  Print(L"Memory map retrieved successfully\r\n");                            // Сообщаем, что карта получена успешно
  Print(L"mmap = %p\r\n", mmap);                                              // Печатаем адрес буфера, где лежит карта памяти UEFI
  Print(L"mmap_size = %lu, descriptor_size = %lu\r\n", mmap_size, dscr_size); // Печатаем общий размер карты памяти и размер одного дескриптора

  // Перепаковываем карту памяти
  s = ConvertMemoryMap(mmap, mmap_size, dscr_size, &new_mmap);
  if (EFI_ERROR(s)) {
    Print(L"ConvertMemoryMap error: %r\r\n", s);
    gBS->FreePool(mmap);
    return s;
  }

  Print(L"New mmap = %p\r\n", new_mmap);  // Печатаем адрес нашей новой карты памяти
  PrintMemoryMap(new_mmap);               // Печатаем новую, перепакованную карту памяти на экран
  gBS->FreePool(new_mmap);                // Освобождаем память выделенную под новую карту памяти
  gBS->FreePool(mmap);                    // Освобождаем память выделенную под исходную карту памяти UEFI

  return EFI_SUCCESS;
}