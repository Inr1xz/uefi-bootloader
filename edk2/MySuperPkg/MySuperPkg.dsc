[Defines]
  PLATFORM_NAME              = MySuperPkg
  PLATFORM_GUID              = cf9f1ca2-c7ab-48d9-900b-18197216f4e4
  PLATFORM_VERSION           = 1.0
  DSC_SPECIFICATION          = 0x00010005
  SUPPORTED_ARCHITECTURES    = X64
  BUILD_TARGETS              = DEBUG|RELEASE
  SKUID_IDENTIFIER           = DEFAULT

[SkuIds]
  0|DEFAULT

[LibraryClasses]
  # точка входа UEFI-приложения
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf

  # базовые библиотеки
  BaseLib                    | MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib              | MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  UefiLib                    | MdePkg/Library/UefiLib/UefiLib.inf
  PrintLib                   | MdePkg/Library/BasePrintLib/BasePrintLib.inf
  MemoryAllocationLib        | MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  UefiBootServicesTableLib   | MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib| MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  DebugLib                   | MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  PcdLib                     | MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  RegisterFilterLib          | MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  DevicePathLib              | MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  StackCheckLib              | MdePkg/Library/StackCheckLib/StackCheckLib.inf
  StackCheckFailureHookLib   | MdePkg/Library/StackCheckFailureHookLibNull/StackCheckFailureHookLibNull.inf

[Packages]
  MdePkg/MdePkg.dec
  MySuperPkg/MySuperPkg.dec

[Components]
  MySuperPkg/HelloWorld/HelloWorld.inf
  MySuperPkg/MemMap/MemMap.inf  
