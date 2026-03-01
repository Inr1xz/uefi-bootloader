#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

typedef struct {
  UINT32               Type;
  EFI_PHYSICAL_ADDRESS PhysStart;
  UINT64               NumPages;
  UINT64               Attribute;
} MY_MEMORY_REGION;

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS           Status;
  EFI_MEMORY_DESCRIPTOR *MemMap = NULL;
  UINTN                MemMapSize = 0;
  UINTN                MapKey;
  UINTN                DescriptorSize;
  UINT32               DescriptorVersion;


  // 1. Первый вызов GetMemoryMap — узнать нужный размер буфера
  Status = gBS->GetMemoryMap(
              &MemMapSize,
              MemMap,
              &MapKey,
              &DescriptorSize,
              &DescriptorVersion
            );

  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print(L"GetMemoryMap (probe) error: %r\r\n", Status);
    return Status;
  }

  // небольшой запас
  MemMapSize += 2 * DescriptorSize;


  // 2. Выделяем память под карту
  Status = gBS->AllocatePool(EfiLoaderData, MemMapSize, (VOID**)&MemMap);
  if (EFI_ERROR(Status)) {
    Print(L"AllocatePool error: %r\r\n", Status);
    return Status;
  }


  // 3. Второй вызов GetMemoryMap — уже с буфером
  Status = gBS->GetMemoryMap(
              &MemMapSize,
              MemMap,
              &MapKey,
              &DescriptorSize,
              &DescriptorVersion
            );
  if (EFI_ERROR(Status)) {
    Print(L"GetMemoryMap error: %r\r\n", Status);
    gBS->FreePool(MemMap);
    return Status;
  }

  
  // 4. Перепаковываем в свою структуру и печатаем
  UINTN EntryCount = MemMapSize / DescriptorSize;
  UINT8 *Walker = (UINT8*)MemMap;

  Print(L"Idx  Type  PhysStart          Pages    Attr\r\n");
  Print(L"-------------------------------------------------------\r\n");

  for (UINTN Index = 0; Index < EntryCount; Index++) {
    EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR*)Walker;

    MY_MEMORY_REGION Region;
    Region.Type      = Desc->Type;
    Region.PhysStart = Desc->PhysicalStart;
    Region.NumPages  = Desc->NumberOfPages;
    Region.Attribute = Desc->Attribute;

    Print(
      L"%3u  %4u  %016lx  %8lu  %lx\r\n",
      Index,
      Region.Type,
      Region.PhysStart,
      Region.NumPages,
      Region.Attribute
    );

    Walker += DescriptorSize;
  }

  gBS->FreePool(MemMap);
  return EFI_SUCCESS;
}