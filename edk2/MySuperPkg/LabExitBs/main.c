#include <Uefi.h>

#include <Library/PrintLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>

#include "Uefi/UefiBaseType.h"
#include "kernel.inc"

typedef unsigned char u8;
typedef unsigned long long u64;
typedef unsigned u32;

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

static SystemInfo info;

#define COLOR 0x00ff00

typedef struct _Context {
  EFI_MP_SERVICES_PROTOCOL *mp;
} Context;

BOOLEAN context_setup_mp(Context *self) {
  EFI_MP_SERVICES_PROTOCOL *mp;
  EFI_GUID mp_guid = EFI_MP_SERVICES_PROTOCOL_GUID;
  EFI_STATUS s;

  s = gBS->LocateProtocol(&mp_guid, NULL, (void **)&mp);

  if (EFI_ERROR(s)) {
    Print(L"Error while locating mp service protocol\r\n");
    return FALSE;
  }

  self->mp = mp;

  return TRUE;
}

UINTN context_processors_count(Context *self) {
  UINTN count = 0;
  UINTN enabled = 0;

  self->mp->GetNumberOfProcessors(self->mp, &count, &enabled);

  return count;
}

EFI_PROCESSOR_INFORMATION context_get_processor_info(Context *self,
                                                     UINTN index) {
  EFI_PROCESSOR_INFORMATION info = {0};

  self->mp->GetProcessorInfo(self->mp, index, &info);

  return info;
}

EFI_STATUS GetGop(EFI_GRAPHICS_OUTPUT_PROTOCOL **Gop) {
  EFI_HANDLE *HandleBuffer;
  UINTN HandleCount = 0, i = 0;
  EFI_STATUS Status = EFI_SUCCESS;

  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                   NULL, &HandleCount, &HandleBuffer);

  if (EFI_ERROR(Status)) {
    Print(L"failed to LocateHandleBuffer for GOP: %r\r\n", Status);
    return Status;
  }

  for (i = 0; i < HandleCount; ++i) {
    Status = gBS->HandleProtocol(HandleBuffer[i],
                                 &gEfiGraphicsOutputProtocolGuid, (void **)Gop);

    if (Status == EFI_SUCCESS) {
      break;
    }
  }

  return Status;
}

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status = EFI_SUCCESS;
  UINTN MapKey = 0;
  UINTN DescriptorSize = 0;
  UINT32 DescriptorVersion = 0;
  UINTN MemoryMapSize = 0;
  EFI_MEMORY_DESCRIPTOR *MemoryMap = (EFI_MEMORY_DESCRIPTOR *)0;

  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;

  UINT64 VideoBufferAddress = 0;
  UINT32 VerticalRes = 0, HorisontalRes = 0, i = 0;
  (void)i;

  Status = GetGop(&Gop);

  if (EFI_ERROR(Status)) {
    Print(L"Failed to get Gop\r\n");
    return Status;
  }

  VideoBufferAddress = Gop->Mode->FrameBufferBase;
  HorisontalRes = Gop->Mode->Info->HorizontalResolution;
  VerticalRes = Gop->Mode->Info->VerticalResolution;

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

    Print(L"%llu %u %u %u %u\r\n", efi_proc_info.ProcessorId,
          efi_proc_info.StatusFlag, efi_proc_info.Location.Core,
          efi_proc_info.Location.Package, efi_proc_info.Location.Thread);

    info.processors.ids[i] = efi_proc_info.ProcessorId;
  }

  Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey,
                             &DescriptorSize, &DescriptorVersion);

  if (Status == EFI_BUFFER_TOO_SMALL) {
    Status = gBS->AllocatePool(EfiBootServicesData,
                               MemoryMapSize + 5 * DescriptorSize,
                               (void **)&MemoryMap);
    if (EFI_ERROR(Status)) {
      Print(L"Failed to allocate mem for MemoryMap: %r\r\n", Status);
      return Status;
    }

    Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey,
                               &DescriptorSize, &DescriptorVersion);

    if (EFI_ERROR(Status)) {
      Print(L"Failed to Get MemoryMap: %r\r\n", Status);
      return Status;
    }
  }

  Status = gBS->ExitBootServices(ImageHandle, MapKey);

  if (EFI_ERROR(Status)) {
    Print(L"ExitBootServices failed: %r\r\n", Status);
    return Status;
  }

  // for (i = 0; i < (VerticalRes * HorisontalRes); ++i) {
  // 	*((UINT32*)VideoBufferAddress + i) = ((i / HorisontalRes) & 0xff << 8) |
  // ((i % HorisontalRes) & 0xff);
  // }

  info.video.buffer = VideoBufferAddress;
  info.video.height = VerticalRes;
  info.video.width = HorisontalRes;

  unsigned char *dst = (void *)0x2000;
  for (unsigned int i = 0; i < kernel_len; ++i) {
    dst[i] = kernel[i];
  }

  __asm__ __volatile__("mov $0x2000, %%rdi\n\t"
                       "mov %0, %%rax\n\t"
                       "jmp %%rdi\n\t"
                       :
                       : "r"(&info)
                       :);

  // __builtin_unreachable();

  return EFI_SUCCESS;
}
