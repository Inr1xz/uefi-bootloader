CC = gcc
CFLAGS = -Os -fshort-wchar -fno-builtin -nostdlib -fno-strict-aliasing \
				 -Wall -Werror -Wno-array-bounds -fno-common -fstack-protector \
				 -ffunction-sections -fdata-sections -mstack-protector-guard=global \
				 -m64 -maccumulate-outgoing-args -mno-red-zone -Wno-address \
				 -mcmodel=small -fpie -fno-asynchronous-unwind-tables -Wno-address \
				 -fno-omit-frame-pointer -flto -Wno-unused-but-set-variable \
				 -Wno-unused-const-variable -nostdlib -Wl,-n,-q,--gc-sections \
				 -z common-page-size=0x40 -Wl,--entry,_entry -u _entry \
				 -Wl,-melf_x86_64,--oformat=elf64-x86-64,-pie -Wl,--fatal-warnings \
				 -Wl,-z,noexecstack -Wl,-z,notext -flto -Os -fshort-wchar \
				 -fno-builtin -fno-strict-aliasing -Wall -Werror -Wno-array-bounds \
				 -fno-common -fstack-protector -ffunction-sections -fdata-sections \
				 -mstack-protector-guard=global -m64 -maccumulate-outgoing-args \
				 -mno-red-zone -Wno-address -mcmodel=small -fpie \
				 -fno-asynchronous-unwind-tables -Wno-address -fno-omit-frame-pointer \
				 -flto -Wno-unused-but-set-variable -Wno-unused-const-variable \
				 -Wl,--script=linker.lds -Wno-error

SOURCES = $(shell find -name "*.c" -type f)

.PHONY: all
all: kernel

.PHONY: dump
dump:
	@echo ${SOURCES}

entry.o: entry.nasm
	nasm -f elf64 -o entry.o entry.nasm

kernel.elf: ${SOURCES} entry.o processor_init.inc
	${CC} ${CFLAGS} entry.o -o kernel.elf ${SOURCES}

kernel: kernel.elf
	objcopy -O binary kernel.elf kernel

processor_init.inc: processor_init
	xxd -i processor_init > processor_init.inc

processor_init: processor_init.nasm
	nasm -f bin processor_init.nasm

font.inc: font-gen.py
	python3 font-gen.py > font.inc
