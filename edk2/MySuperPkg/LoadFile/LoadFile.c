#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

EFI_STATUS
ReadFileToMemory(
  IN  CHAR16   *FilePath,
  OUT VOID     **Buffer,
  OUT UINTN    *BufferSize
)
{
  EFI_STATUS                         Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL    *Fs;
  EFI_FILE_PROTOCOL                  *Root;
  EFI_FILE_PROTOCOL                  *File;
  EFI_FILE_INFO                      *FileInfo;
  UINTN                              FileInfoSize;

  Fs = NULL;
  Root = NULL;
  File = NULL;
  FileInfo = NULL;
  *Buffer = NULL;
  *BufferSize = 0;

  //
  // 1. Найти файловую систему
  //
  Status = gBS->LocateProtocol(
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  (VOID **)&Fs
                );
  if (EFI_ERROR(Status)) {
    Print(L"LocateProtocol(SimpleFileSystem) failed: %r\r\n", Status);
    return Status;
  }

  //
  // 2. Открыть корень тома
  //
  Status = Fs->OpenVolume(Fs, &Root);
  if (EFI_ERROR(Status)) {
    Print(L"OpenVolume failed: %r\r\n", Status);
    return Status;
  }

  //
  // 3. Открыть нужный файл
  //
  Status = Root->Open(
                   Root,
                   &File,
                   FilePath,
                   EFI_FILE_MODE_READ,
                   0
                 );
  if (EFI_ERROR(Status)) {
    Print(L"Open file '%s' failed: %r\r\n", FilePath, Status);
    Root->Close(Root);
    return Status;
  }

  //
  // 4. Узнать размер структуры EFI_FILE_INFO
  //
  FileInfoSize = 0;
  Status = File->GetInfo(
                   File,
                   &gEfiFileInfoGuid,
                   &FileInfoSize,
                   NULL
                 );

  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print(L"GetInfo(size probe) failed: %r\r\n", Status);
    File->Close(File);
    Root->Close(Root);
    return Status;
  }

  //
  // 5. Выделить память под EFI_FILE_INFO
  //
  Status = gBS->AllocatePool(
                  EfiBootServicesData,
                  FileInfoSize,
                  (VOID **)&FileInfo
                );
  if (EFI_ERROR(Status)) {
    Print(L"AllocatePool(FileInfo) failed: %r\r\n", Status);
    File->Close(File);
    Root->Close(Root);
    return Status;
  }

  //
  // 6. Получить информацию о файле
  //
  Status = File->GetInfo(
                   File,
                   &gEfiFileInfoGuid,
                   &FileInfoSize,
                   FileInfo
                 );
  if (EFI_ERROR(Status)) {
    Print(L"GetInfo(FileInfo) failed: %r\r\n", Status);
    gBS->FreePool(FileInfo);
    File->Close(File);
    Root->Close(Root);
    return Status;
  }

  //
  // 7. Размер файла
  //
  *BufferSize = (UINTN)FileInfo->FileSize;

  Print(L"File size: %lu bytes\r\n", *BufferSize);

  //
  // 8. Выделить память под содержимое файла
  //
  Status = gBS->AllocatePool(
                  EfiBootServicesData,
                  *BufferSize,
                  Buffer
                );
  if (EFI_ERROR(Status)) {
    Print(L"AllocatePool(Buffer) failed: %r\r\n", Status);
    gBS->FreePool(FileInfo);
    File->Close(File);
    Root->Close(Root);
    return Status;
  }

  //
  // 9. Прочитать файл в память
  //
  Status = File->Read(
                   File,
                   BufferSize,
                   *Buffer
                 );
  if (EFI_ERROR(Status)) {
    Print(L"Read failed: %r\r\n", Status);
    gBS->FreePool(*Buffer);
    *Buffer = NULL;
    *BufferSize = 0;
    gBS->FreePool(FileInfo);
    File->Close(File);
    Root->Close(Root);
    return Status;
  }

  //
  // 10. Освободить временные структуры и закрыть файл
  //
  gBS->FreePool(FileInfo);
  File->Close(File);
  Root->Close(Root);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
)
{
  EFI_STATUS Status;
  VOID       *FileBuffer;
  UINTN      FileSize;
  UINT8      *Bytes;

  FileBuffer = NULL;
  FileSize = 0;

  //
  // Пробуем загрузить файл с диска в память
  //
  Status = ReadFileToMemory(L"\\EFI\\BOOT\\KERNEL.BIN", &FileBuffer, &FileSize);
  if (EFI_ERROR(Status)) {
    Print(L"ReadFileToMemory failed: %r\r\n", Status);
    return Status;
  }

  Print(L"File loaded successfully!\r\n");
  Print(L"Buffer address: %p\r\n", FileBuffer);
  Print(L"Buffer size   : %lu\r\n", FileSize);

  //
  // Для проверки печатаем первые 16 байт файла
  //
  Bytes = (UINT8 *)FileBuffer;

  Print(L"First bytes: ");
  for (UINTN i = 0; i < 16 && i < FileSize; ++i) {
    Print(L"%02x ", Bytes[i]);
  }
  Print(L"\r\n");

  //
  // Пока просто освобождаем память.
  // В будущем здесь можно будет передавать буфер ядру.
  //
  gBS->FreePool(FileBuffer);

  return EFI_SUCCESS;
}