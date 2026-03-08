#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

typedef enum {
  MmapEntryType_NotAvailable,
  MmapEntryType_Available
} MmapEntryType;

typedef struct _MmapEntry {
  UINT64 addr;   // Начальный адрес региона
  UINT64 size;   // Размер региона в байтах
  UINT8  type;   // Наш упрощённый тип: доступен / недоступен
} MmapEntry;

typedef struct _Mmap {
  UINT64 size;       // Количество записей
  MmapEntry entries[];
} Mmap;

/**
  Получить карту памяти UEFI.
  Функция сама обрабатывает EFI_BUFFER_TOO_SMALL в цикле.

  @param[out] OutMemoryMap        Указатель на буфер с картой памяти
  @param[out] OutMemoryMapSize    Размер карты памяти в байтах
  @param[out] OutMapKey           Ключ карты памяти
  @param[out] OutDescriptorSize   Размер одного дескриптора
  @param[out] OutDescriptorVersion Версия дескриптора

  @retval EFI_SUCCESS             Карта памяти успешно получена
  @retval другое значение         Ошибка
**/
EFI_STATUS
GetUefiMemoryMap(
  EFI_MEMORY_DESCRIPTOR **OutMemoryMap,
  UINTN *OutMemoryMapSize,
  UINTN *OutMapKey,
  UINTN *OutDescriptorSize,
  UINT32 *OutDescriptorVersion
)
{
  EFI_STATUS s;
  EFI_MEMORY_DESCRIPTOR *mmap = NULL;
  UINTN mmap_size = 0;
  UINTN mmap_key = 0;
  UINTN dscr_size = 0;
  UINT32 dscr_vers = 0;

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

/**
  Преобразовать стандартную карту памяти UEFI в нашу упрощённую структуру.

  @param[in]  mmap        Исходная карта памяти UEFI
  @param[in]  mmap_size   Общий размер карты памяти
  @param[in]  dscr_size   Размер одного дескриптора
  @param[out] OutNewMap   Новый перепакованный mmap

  @retval EFI_SUCCESS     Успех
  @retval другое значение Ошибка
**/
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