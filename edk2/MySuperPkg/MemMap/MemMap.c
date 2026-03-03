// Логика программы, непосредственно UEFI-приложение 
/*
1. Через gBS->GetMemoryMap() дважды получает карту памяти:
    первый раз — чтобы узнать нужный размер буфера,
    второй раз — уже с выделенной памятью.
2. Перепаковывает каждую запись EFI_MEMORY_DESCRIPTOR в свою структуру MY_MEMORY_REGION.
3. Печатает табличку: индекс, тип, физический адрес, количество страниц, атрибуты.
*/
#include <Uefi.h>                               // Базовые типы и определения UEFI (EFI_STATUS, EFI_HANDLE и т.п).
#include <Library/UefiLib.h>                    // Функции Print(), EFIAPI и разные вспомогательные утилиты
#include <Library/UefiBootServicesTableLib.h>   // Доступ к глобальному указателю gBS (Boot Services)
#include <Library/MemoryAllocationLib.h>        // AllocatePool(), FreePool() и работа с памятью


// Собственная структура для хранения информации об одном регионе памяти
// Это "перепакованный" вариант EFI_MEMORY_DESCRIPTOR
typedef struct {
  UINT32               Type;       // Тип памяти (EFI_MEMORY_TYPE)
  EFI_PHYSICAL_ADDRESS PhysStart;  // Физический стартовый адрес региона
  UINT64               NumPages;   // Количество страниц в регионе
  UINT64               Attribute;  // Атрибуты (битовая маска EFI_MEMORY_xxx)
} MY_MEMORY_REGION;

// Точка входа UEFI-приложения
// Сигнатура задаётся UefiApplicationEntryPoint + ENTRY_POINT = UefiMain в .inf
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,   // Дескриптор образа этого приложения
  IN EFI_SYSTEM_TABLE  *SystemTable   // Указатель на системную таблицу UEFI
  )
{
  EFI_STATUS            Status;            // Переменная для кодов возврата из вызовов UEFI
  EFI_MEMORY_DESCRIPTOR *MemMap = NULL;    // Указатель на буфер с картой памяти
  UINTN                 MemMapSize = 0;    // Размер буфера под карту памяти (в байтах)
  UINTN                 MapKey;            // Ключ карты памяти (используется для ExitBootServices)
  UINTN                 DescriptorSize;    // Размер структуры EFI_MEMORY_DESCRIPTOR
  UINT32                DescriptorVersion; // Версия структуры дескриптора



  // 1. Первый вызов GetMemoryMap — узнать нужный размер буфера
  // На вход MemMapSize = 0 и MemMap = NULL, поэтому ожидаем EFI_BUFFER_TOO_SMALL
  Status = gBS->GetMemoryMap(
              &MemMapSize,        // [in/out] размер буфера; 0 → вернётся нужный размер
              MemMap,             // [out] буфер (пока NULL)
              &MapKey,            // [out] ключ карты 
              &DescriptorSize,    // [out] размер одной записи карты памяти
              &DescriptorVersion  // [out] версия дескриптора
            );

  // Если вернулся не EFI_BUFFER_TOO_SMALL, значит что-то пошло не по "стандартному" сценарию:
  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print(L"GetMemoryMap (probe) error: %r\r\n", Status);   // %r — форматтер для EFI_STATUS
    return Status;                                          // Завершаем с ошибкой
  }

  // небольшой запас к размеру карты памяти — плюс 2 дескриптора
  // если между первым и вторым вызовом карта немного изменится
  MemMapSize += 2 * DescriptorSize;


  // 2. Выделяем память под карту
  Status = gBS->AllocatePool(
             EfiLoaderData,       // Тип выделяемой памяти (Loader Data)
             MemMapSize,          // Сколько байт выделить
             (VOID**)&MemMap      // Указатель на результат (EFI_MEMORY_DESCRIPTOR *)
           );
  if (EFI_ERROR(Status)) {        // Макрос EFI_ERROR проверяет, является ли код статусом ошибки
    Print(L"AllocatePool error: %r\r\n", Status);
    return Status;
  }


  // 3. Второй вызов GetMemoryMap — уже с буфером
  Status = gBS->GetMemoryMap(
              &MemMapSize,        // [in/out] размер буфера на входе, фактический размер на выходе
              MemMap,             // [out] заполненный буфер с дескрипторами
              &MapKey,            // [out] ключ карты памяти
              &DescriptorSize,    // [out] (вновь) размер дескриптора
              &DescriptorVersion  // [out] версия дескриптора
            );
  if (EFI_ERROR(Status)) {
    Print(L"GetMemoryMap error: %r\r\n", Status);
    gBS->FreePool(MemMap);        // Освобождаем выделенную память перед выходом
    return Status;
  }

  
  // 4. Перепаковываем в свою структуру и печатаем
  UINTN EntryCount = MemMapSize / DescriptorSize; // Количество EFI_MEMORY_DESCRIPTOR в буфере
  UINT8 *Walker = (UINT8*)MemMap;                 // Указатель-итератор по буферу в виде массива байт


  Print(L"Idx  Type  PhysStart          Pages    Attr\r\n");
  Print(L"-------------------------------------------------------\r\n");

  // Цикл по всем дескрипторам карты памяти
  for (UINTN Index = 0; Index < EntryCount; Index++) {
    EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR*)Walker;

    MY_MEMORY_REGION Region;
    Region.Type      = Desc->Type;
    Region.PhysStart = Desc->PhysicalStart;
    Region.NumPages  = Desc->NumberOfPages;
    Region.Attribute = Desc->Attribute;

    // Печатаем одну строку таблицы с информацией об этом регионе
    Print(
      L"%3u  %4u  %016lx  %8lu  %lx\r\n",
      Index,              // Номер записи
      Region.Type,        // Тип региона (число)
      Region.PhysStart,   // Физический стартовый адрес
      Region.NumPages,    // Количество страниц
      Region.Attribute    // Атрибуты региона
    );
    // Переходим к следующему дескриптору
    Walker += DescriptorSize;
  }
  // Освобождаем память, выделенную под карту памяти
  gBS->FreePool(MemMap);
  // Возвращаем EFI_SUCCESS — успешное завершение приложения
  return EFI_SUCCESS;
}