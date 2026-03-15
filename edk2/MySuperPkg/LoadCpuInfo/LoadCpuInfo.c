#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>  

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
    EFI_MP_SERVICES_PROTOCOL *MpServices = NULL;
    UINTN TotalProcessors = 0;
    UINTN EnabledProcessors = 0;

    // 1. Найти протокол мультипроцессорных сервисов
    Status = gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (VOID **)&MpServices);
    if (EFI_ERROR(Status)) {
        Print(L"Cannot locate MP Services Protocol: %r\n", Status);
        return Status;
    }

    // 2. Получить количество процессоров
    Status = MpServices->GetNumberOfProcessors(MpServices, &TotalProcessors, &EnabledProcessors);
    if (EFI_ERROR(Status)) {
        Print(L"GetNumberOfProcessors failed: %r\n", Status);
        return Status;
    }

    // 3. Заголовок таблицы
    Print(L"\nTotal processors: %u, Enabled processors: %u\n", TotalProcessors, EnabledProcessors);
    Print(L"-----------------------------------------------------------------------------------------------------------------------------------\n");
    Print(L"Idx | APIC ID            | En | BSP | Hlth | StatusFlag  | Pkg | Core | Thr | ExPkg | ExMod | ExTile | ExDie | ExCore | ExThr |\n");
    Print(L"-----------------------------------------------------------------------------------------------------------------------------------\n");

    // 4. Вывод информации по каждому процессору
    for (UINTN i = 0; i < TotalProcessors; i++) {
        EFI_PROCESSOR_INFORMATION ProcInfo;
        EFI_PROCESSOR_INFORMATION ProcInfoEx;
        BOOLEAN HasExtendedTopology;
        BOOLEAN Enabled;
        BOOLEAN Bsp;
        BOOLEAN Healthy;

        Status = MpServices->GetProcessorInfo(MpServices, i, &ProcInfo);
        if (EFI_ERROR(Status)) {
            Print(L"Processor %u: info not available\n", i);
            continue;
        }

        Enabled = (ProcInfo.StatusFlag & PROCESSOR_ENABLED_BIT) != 0;
        Bsp     = (ProcInfo.StatusFlag & PROCESSOR_AS_BSP_BIT) != 0;
        Healthy = (ProcInfo.StatusFlag & PROCESSOR_HEALTH_STATUS_BIT) != 0;

        Status = MpServices->GetProcessorInfo(MpServices, i | CPU_V2_EXTENDED_TOPOLOGY, &ProcInfoEx);
        HasExtendedTopology = !EFI_ERROR(Status);

        if (HasExtendedTopology) {
            Print(
              L"%3u | 0x%016lx | %2s | %3s | %4s | 0x%08x  | %3u | %4u | %3u | %5u | %5u | %6u | %5u | %6u | %5u |\n",
              i,
              ProcInfo.ProcessorId,
              BoolToText(Enabled),
              BoolToText(Bsp),
              BoolToText(Healthy),
              ProcInfo.StatusFlag,
              ProcInfo.Location.Package,
              ProcInfo.Location.Core,
              ProcInfo.Location.Thread,
              ProcInfoEx.ExtendedInformation.Location2.Package,
              ProcInfoEx.ExtendedInformation.Location2.Module,
              ProcInfoEx.ExtendedInformation.Location2.Tile,
              ProcInfoEx.ExtendedInformation.Location2.Die,
              ProcInfoEx.ExtendedInformation.Location2.Core,
              ProcInfoEx.ExtendedInformation.Location2.Thread
              );
        } else {
            Print(
              L"%3u | 0x%016lx | %2s | %3s | %4s | 0x%08x  | %3u | %4u | %3u | %5s | %5s | %6s | %5s | %6s | %5s |\n",
              i,
              ProcInfo.ProcessorId,
              BoolToText(Enabled),
              BoolToText(Bsp),
              BoolToText(Healthy),
              ProcInfo.StatusFlag,
              ProcInfo.Location.Package,
              ProcInfo.Location.Core,
              ProcInfo.Location.Thread,
              L"-",
              L"-",
              L"-",
              L"-",
              L"-",
              L"-"
              );
        }
    }

    Print(L"-----------------------------------------------------------------------------------------------------------------------------------\n\n");

    return EFI_SUCCESS;
}
