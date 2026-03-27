CROSS_COMPILE = riscv64-unknown-elf-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
MKDIR = mkdir -p
RM = rm -rf

CFLAGS = -nostdlib -nostartfiles -ffreestanding -fno-builtin
CFLAGS += -Wall -g -O0
CFLAGS += -march=rv64imac -mabi=lp64 -mcmodel=medany
CFLAGS += -I include

OUTPUT = out

SRCS_ASM = bootloader/entry.S \
		bootloader/trap_entry.S \
		kernel/trap/trap_entry.S \
		kernel/proc/switch.S
SRCS_C = bootloader/main.c \
		bootloader/trap.c \
		bootloader/sbi.c \
		kernel/main.c \
		kernel/driver/uart.c \
		kernel/driver/plic.c \
		kernel/lib/printf.c \
		kernel/lib/string.c \
		kernel/trap/trap.c \
		kernel/trap/irq.c \
		kernel/trap/exception.c \
		kernel/trap/syscall.c \
		kernel/trap/timer.c \
		kernel/mm/pmm.c \
		kernel/mm/page_table.c \
		kernel/mm/vmm.c \
		kernel/proc/proc.c \
		kernel/proc/scheduler.c \
		kernel/proc/elf.c


OBJS_ASM = $(addprefix $(OUTPUT)/, $(patsubst %.S, %.o, $(SRCS_ASM)))
OBJS_C   = $(addprefix $(OUTPUT)/, $(patsubst %.c, %.o, $(SRCS_C)))
OBJS     = $(OBJS_ASM) $(OBJS_C)


ELF = $(OUTPUT)/os.elf
BIN = $(OUTPUT)/os.bin

LDFLAGS = -T linker.ld

# ---- 用户程序 ----
USER_CC = $(CC)
USER_CFLAGS = -nostdlib -nostartfiles -ffreestanding -fno-builtin
USER_CFLAGS += -Wall -g -O0
USER_CFLAGS += -march=rv64imac -mabi=lp64 -mcmodel=medany

USER_SRCS = user/start.S \
			user/usys.S \
			user/hello.c  \
			user/uprintf.c
USER_ELF  = user/hello
USER_OBJ  = $(OUTPUT)/user/hello_bin.o

$(USER_ELF): $(USER_SRCS) user/user.ld user/user.h
	$(USER_CC) $(USER_CFLAGS) -T user/user.ld -o $@ \
		user/start.S user/usys.S user/hello.c user/uprintf.c

$(USER_OBJ): $(USER_ELF)
	@$(MKDIR) $(dir $@)
	$(OBJCOPY) -I binary -O elf64-littleriscv -B riscv $< $@


QEMU   = qemu-system-riscv64
QFLAGS = -nographic -smp 1 -machine virt -bios none -m 128M

GDB = gdb-multiarch

.DEFAULT_GOAL := all
all: $(ELF)

$(ELF): $(OBJS) $(USER_OBJ) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(USER_OBJ)
	$(OBJCOPY) -O binary $@ $(BIN)

$(OUTPUT)/%.o: %.c
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTPUT)/%.o: %.S
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	@echo "Press Ctrl-A then X to exit QEMU"
	@echo "----------------------------------"
	$(QEMU) $(QFLAGS) -kernel $(ELF)

.PHONY: debug
debug: all
	@echo "Press Ctrl-C then type 'quit' to exit GDB and QEMU"
	@echo "---------------------------------------------------"
	$(QEMU) $(QFLAGS) -kernel $(ELF) -s -S &
	$(GDB) $(ELF) -q -ex "target remote :1234"

.PHONY: code
code: all
	$(OBJDUMP) -S $(ELF) | less

.PHONY: clean
clean:
	$(RM) $(OUTPUT)
	$(RM) $(USER_ELF)