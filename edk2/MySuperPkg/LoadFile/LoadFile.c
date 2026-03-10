#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>


/*
Функция загружает файл с диска в память
1) Находим файловую систему
2) Открываем корень тома
3) Открываем нужный файл
4) Узнаем размер файла
5) Выделяем память под содержимое файла
6) Читаем файл в память
*/
EFI_STATUS
ReadFileToMemory(
  IN  CHAR16   *FilePath,   // Путь к файлу в формате UEFI (CHAR16-строка)
  OUT VOID     **Buffer,    // Сюда функция вернёт адрес буфера с содержимым файлы
  OUT UINTN    *BufferSize  // Сюда функция вернет размер файла в байтах
)
{
  EFI_STATUS                         Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL    *Fs;           // Указатель на протокол файловой системы UEFI (через него мы открываем корень тома)
  EFI_FILE_PROTOCOL                  *Root;         // Указатель на корневой каталог тома (после OpenVolum() Root будет представлять корень файловой системы)
  EFI_FILE_PROTOCOL                  *File;         // Указатель на открытый файл (после Root->Open здесть будет дескриптор нужного файла)
  EFI_FILE_INFO                      *FileInfo;     // Указатель на структуру EFI_FILE_INFO в ней хранится информация о файле (размер, атрибуты, имя и тд)
  UINTN                              FileInfoSize;  // Размер буфера под EFI_FILE_INFO (изначально мы его не знаем)

  Fs = NULL;
  Root = NULL;
  File = NULL;
  FileInfo = NULL;
  *Buffer = NULL;
  *BufferSize = 0;

  // 1. Найти файловую систему

  /* 
  Ищем протокол файловой системы UEFI 
  LocateProtocol(): Ищет первый доступный экземпляр указанного протокола (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL)
  */
  Status = gBS->LocateProtocol(
                  &gEfiSimpleFileSystemProtocolGuid,      // GUID протокола который мы ищем
                  NULL,                                   // Registration = NULL
                  (VOID **)&Fs                            // Адрес переменной, куда вернётся указатель на протокол
                );
  if (EFI_ERROR(Status)) {
    Print(L"LocateProtocol(SimpleFileSystem) failed: %r\r\n", Status);
    return Status;
  }

  
  // 2. Открыть корень тома
  
  /*
  OpenVolume() открывает корень файловой системы
  После этого Root — это уже EFI_FILE_PROTOCOL,
  через который можно открывать файлы и каталоги на этом томе
  */
  Status = Fs->OpenVolume(Fs, &Root);
  if (EFI_ERROR(Status)) {
    Print(L"OpenVolume failed: %r\r\n", Status);
    return Status;
  }


  // 3. Открыть нужный файл

  /*
  Открываем файла по пути FilePath
  */
  Status = Root->Open(
                   Root,                // каталог, от которого открываем файл
                   &File,               // сюда вернется дескриптор файла
                   FilePath,            // путь к файлу
                   EFI_FILE_MODE_READ,  // открыть только на чтение
                   0                    // без специальных атрибутов
                 );
  if (EFI_ERROR(Status)) {
    Print(L"Open file '%s' failed: %r\r\n", FilePath, Status);
    Root->Close(Root);
    return Status;
  }


  // 4. Узнать размер структуры EFI_FILE_INFO

  /*
  Вызываем GetInfo с NULL-буфером, чтобы узнать сколько памяти нужно под EFI_FILE_INFO
  gEfiFileInfoGuid говорит:
  "я хочу получить именно информацию о файле в формате EFI_FILE_INFO"
  */
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


  // 5. Выделить память под EFI_FILE_INFO

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


  // 6. Получить информацию о файле

  /*
  Теперь повторяем GetInfo(), но уже с нормальным буфером
  После этого в FileInfo будет лежать информация о файле
  */
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


  // 7. Размер файла

  /*
  Берем размер файла из структуры EFI_FILE_INFO
  FileSize - длина файла в байтах
  */
  *BufferSize = (UINTN)FileInfo->FileSize;

  Print(L"File size: %lu bytes\r\n", *BufferSize);

  // 8. Выделить память под содержимое файла
  
  /*
  Выделяем память под содержимое файла
  После этого вызова *Buffer будет указывать на буфер, куда мы прочитаем файл целиком
  */
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


  // 9. Прочитать файл в память

  /*
  Читамем файл в раннее выделенный буфер
  */
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
  gBS->FreePool(FileInfo);      // Овобождаем FileInfo
  File->Close(File);            // Закрываем файл
  Root->Close(Root);            // Закрываем корень тома

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
  VOID       *FileBuffer;   // Сюда наша функция записывает адрес буфера с содержимым файлв
  UINTN      FileSize;      // Сюда наша функция запишет размер загруженного файла
  UINT8      *Bytes;        // Указатель на байтовое представление загруженного файла (чтобы печатать первые байты файла)

  FileBuffer = NULL;
  FileSize = 0;

  
  // Пробуем загрузить файл с диска в память
  /*
  Вызываем нашу функцию и просим ее прочитать файл по пути
  */
  Status = ReadFileToMemory(L"\\EFI\\BOOT\\KERNEL.BIN", &FileBuffer, &FileSize);
  if (EFI_ERROR(Status)) {
    Print(L"ReadFileToMemory failed: %r\r\n", Status);
    return Status;
  }

  Print(L"File loaded successfully!\r\n");
  Print(L"Buffer address: %p\r\n", FileBuffer);
  Print(L"Buffer size   : %lu\r\n", FileSize);

  
  // Для проверки печатаем первые 16 байт файла
  // Приводим указатель на буфер к типу UINT8 чтобы обращаться к нему как к массиву байтов
  Bytes = (UINT8 *)FileBuffer;

  Print(L"First bytes: ");
  // Проходим по первым 16 байтам файла
  for (UINTN i = 0; i < 16 && i < FileSize; ++i) {
    // Печатаем каждый байт в 16-ричном виде
    Print(L"%02x ", Bytes[i]);
  }
  Print(L"\r\n");

  
  // освобождаем память
  
  gBS->FreePool(FileBuffer);

  return EFI_SUCCESS;
}