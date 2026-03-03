# Описание платформы/конфигурации сборки
[Defines]
# Общие параметры платформы
  PLATFORM_NAME              = MySuperPkg
  PLATFORM_GUID              = cf9f1ca2-c7ab-48d9-900b-18197216f4e4
  PLATFORM_VERSION           = 1.0
  DSC_SPECIFICATION          = 0x00010005
  SUPPORTED_ARCHITECTURES    = X64                                   # Сборка будет только x64
  BUILD_TARGETS              = DEBUG|RELEASE                         # Разрешено собирать в двух конфигурация: DEBUG и RELEASE
  SKUID_IDENTIFIER           = DEFAULT                               # Идентификатор SKU (вариантов конфигурации)

[SkuIds]
  0|DEFAULT

[LibraryClasses]
# Дословно выглядит так: если какой-то модуль (INF) просит библиотечный класс, то мы вставляем конкретную реализацию по пути
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf          # Реализация точки входа для UEFI-приложений 

# Базовые библиотеки
  BaseLib                    | MdePkg/Library/BaseLib/BaseLib.inf                                           # Базовые функции (битовые операции) 
  BaseMemoryLib              | MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf                               # и тд.
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
# Наши dec файлы
  MdePkg/MdePkg.dec
  MySuperPkg/MySuperPkg.dec

[Components]
# Components говорит какие INF-модули входят в сборку той платформы
  MySuperPkg/HelloWorld/HelloWorld.inf
  MySuperPkg/MemMap/MemMap.inf
