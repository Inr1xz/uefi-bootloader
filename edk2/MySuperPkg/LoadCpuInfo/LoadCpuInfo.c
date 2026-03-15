#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>  

/*
Функция преобразует BOOLEAN в короткий текст для таблицы:
TRUE  -> "Y"
FALSE -> "N"
*/
STATIC
CHAR16 *
BoolToText (
  IN BOOLEAN  Value
  )
{
  return Value ? L"Y" : L"N";
}

EFI_STATUS
EFIAPI
UefiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
)
{
    EFI_STATUS Status;
    EFI_MP_SERVICES_PROTOCOL *MpServices = NULL; // Указатель на протокол мультипроцессорных сервисов
    UINTN TotalProcessors = 0;                   // Общее число логических процессоров в системе
    UINTN EnabledProcessors = 0;                 // Сколько из них сейчас включено

    // 1. Найти протокол мультипроцессорных сервисов
    Status = gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (VOID **)&MpServices); // (VOID **)&MpServices означает, что мы передаем адрес указателя 
    if (EFI_ERROR(Status)) {
        Print(L"Cannot locate MP Services Protocol: %r\r\n", Status);
        return Status;
    }

    // 2. Получить количество процессоров
    Status = MpServices->GetNumberOfProcessors(MpServices, &TotalProcessors, &EnabledProcessors);
    if (EFI_ERROR(Status)) {
        Print(L"GetNumberOfProcessors failed: %r\r\n", Status);
        return Status;
    }

    // 3. Заголовок таблицы
    /*
    Что выводим в таблице:
    Idx      - номер процессора (индекс для GetProcessorInfo)
    ProcessorId - аппаратный ID (на x86/x64 обычно Local APIC ID)
    En/BSP/Hlth - краткие признаки: включен / является BSP (BSP - проццессор который первый начинает выполнять код при запуске системы) / здоров
    StatusFlag  - те же признаки в битовой маске
    Pkg/Core/Thr - базовая топология (пакет/ядро/поток)
    */
    Print(L"\r\nTotal processors: %u, Enabled processors: %u\r\n", TotalProcessors, EnabledProcessors);
    Print(L"-----------------------------------------------------------------------------------------\r\n");
    Print(L"Idx | ProcessorId         | En | BSP | Hlth | StatusFlag  | Pkg | Core | Thr |\r\n");
    Print(L"-----------------------------------------------------------------------------------------\r\n");

    // 4. Вывод информации по каждому процессору
    for (UINTN i = 0; i < TotalProcessors; i++) {
        EFI_PROCESSOR_INFORMATION ProcInfo; // Базовая информация по CPU
        BOOLEAN Enabled;                    // PROCESSOR_ENABLED_BIT
        BOOLEAN Bsp;                        // PROCESSOR_AS_BSP_BIT
        BOOLEAN Healthy;                    // PROCESSOR_HEALTH_STATUS_BIT

        // Запрашиваем базовую информацию о процессоре по его индексу
        Status = MpServices->GetProcessorInfo(MpServices, i, &ProcInfo);
        if (EFI_ERROR(Status)) {
            Print(L"Processor %u: info not available\r\n", i);
            continue;
        }

        // Разбираем биты состояния в удобный вид (Y/N)
        Enabled = (ProcInfo.StatusFlag & PROCESSOR_ENABLED_BIT) != 0;
        Bsp     = (ProcInfo.StatusFlag & PROCESSOR_AS_BSP_BIT) != 0;
        Healthy = (ProcInfo.StatusFlag & PROCESSOR_HEALTH_STATUS_BIT) != 0;

        /*
        Формат вывода по колонкам:
        %3u       -> Idx (индекс CPU)
        0x%016lx  -> ProcessorId (hex, 16 символов, с нулями слева)
        %2s       -> En   ("Y"/"N")
        %3s       -> BSP  ("Y"/"N")
        %4s       -> Hlth ("Y"/"N")
        0x%08x    -> StatusFlag (hex, 8 символов, с нулями слева)
        %3u       -> Pkg
        %4u       -> Core
        %3u       -> Thr
        */
        Print(
          L"%3u | 0x%016lx | %2s | %3s | %4s | 0x%08x  | %3u | %4u | %3u |\r\n",
          i,
          ProcInfo.ProcessorId,
          BoolToText(Enabled),
          BoolToText(Bsp),
          BoolToText(Healthy),
          ProcInfo.StatusFlag,
          ProcInfo.Location.Package,
          ProcInfo.Location.Core,
          ProcInfo.Location.Thread
          );
    }

    Print(L"-----------------------------------------------------------------------------------------\r\n\r\n");

    return EFI_SUCCESS;
}
