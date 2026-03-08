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

  while (1) {
    s = gBS->GetMemoryMap(
              &mmap_size,
              mmap,
              &mmap_key,
              &dscr_size,
              &dscr_vers
            );

    if (s == EFI_SUCCESS) {
      // Успешно получили карту памяти
      *OutMemoryMap = mmap;
      *OutMemoryMapSize = mmap_size;
      *OutMapKey = mmap_key;
      *OutDescriptorSize = dscr_size;
      *OutDescriptorVersion = dscr_vers;
      return EFI_SUCCESS;
    }

    if (s != EFI_BUFFER_TOO_SMALL) {
      // Какая-то другая ошибка
      if (mmap != NULL) {
        gBS->FreePool(mmap);
      }
      return s;
    }

    // Если буфер маленький — освобождаем старый (если был)
    if (mmap != NULL) {
      gBS->FreePool(mmap);
      mmap = NULL;
    }

    // Добавляем небольшой запас, потому что карта памяти
    // может измениться между вызовами
    mmap_size += 2 * dscr_size;

    s = gBS->AllocatePool(EfiBootServicesData, mmap_size, (VOID **)&mmap);
    if (EFI_ERROR(s)) {
      return s;
    }
  }
}


EFI_STATUS
ConvertMemoryMap(
  EFI_MEMORY_DESCRIPTOR *mmap,
  UINTN mmap_size,
  UINTN dscr_size,
  Mmap **OutNewMap
)
{
  EFI_STATUS s;
  UINT64 entries_count = mmap_size / dscr_size;

  UINT64 new_mmap_size = sizeof(UINT64) + entries_count * sizeof(MmapEntry);
  Mmap *new_mmap = NULL;

  s = gBS->AllocatePool(EfiBootServicesData, new_mmap_size, (VOID **)&new_mmap);
  if (EFI_ERROR(s)) {
    return s;
  }

  new_mmap->size = entries_count;

  for (UINT64 i = 0; i < new_mmap->size; ++i) {
    UINTN offset = dscr_size * i;

    EFI_MEMORY_DESCRIPTOR *d =
        (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mmap + offset);

    MmapEntryType t = MmapEntryType_NotAvailable;

    switch (d->Type) {
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

    new_mmap->entries[i].addr = d->PhysicalStart;

    new_mmap->entries[i].size = d->NumberOfPages * SIZE_4KB;

    new_mmap->entries[i].type = t;
  }

  *OutNewMap = new_mmap;
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

  for (UINT64 i = 0; i < mmap->size; ++i) {
    Print(
      L"%016lx %016lx %d\r\n",
      mmap->entries[i].addr,
      mmap->entries[i].size,
      mmap->entries[i].type
    );
  }
}

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

  Mmap *new_mmap = NULL;

  // Получаем карту памяти UEFI в цикле
  s = GetUefiMemoryMap(
        &mmap,
        &mmap_size,
        &mmap_key,
        &dscr_size,
        &dscr_vers
      );

  if (EFI_ERROR(s)) {
    Print(L"GetUefiMemoryMap error: %r\r\n", s);
    return s;
  }

  Print(L"Memory map retrieved successfully\r\n");
  Print(L"mmap = %p\r\n", mmap);
  Print(L"mmap_size = %lu, descriptor_size = %lu\r\n", mmap_size, dscr_size);

  // Перепаковываем карту памяти
  s = ConvertMemoryMap(mmap, mmap_size, dscr_size, &new_mmap);
  if (EFI_ERROR(s)) {
    Print(L"ConvertMemoryMap error: %r\r\n", s);
    gBS->FreePool(mmap);
    return s;
  }

  Print(L"New mmap = %p\r\n", new_mmap);

  // Печатаем новую карту памяти
  PrintMemoryMap(new_mmap);

  // Освобождаем ресурсы
  gBS->FreePool(new_mmap);
  gBS->FreePool(mmap);

  return EFI_SUCCESS;
}