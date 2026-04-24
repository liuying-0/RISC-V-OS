# 阶段 0：环境搭建与项目配置

## 目标

完成本阶段后，你将拥有：
- 可用的 RISC-V 64 位交叉编译工具链（Ubuntu 上）
- 可用的 QEMU RISC-V 64 位模拟器（Ubuntu 上）
- 完整的 `Makefile` 和 `linker.ld`
- 一个能编译出内核镜像并在 QEMU 中运行的项目骨架


**实现顺序参考**：各阶段文件的创建时机和内容演进见 [实现顺序与文件规划](00-implementation-order.md)，可避免依赖顺序问题（如 `memlayout.h` 在阶段 3 即需创建初版）。


---

## 1. 安装工具链（Ubuntu）

```bash
sudo apt update

# RISC-V 64 位裸机工具链
sudo apt install gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf

# 如果上面的包不可用，试试：
# sudo apt install gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
# 前缀会变为 riscv64-linux-gnu-

# QEMU（包含 riscv64）
sudo apt install qemu-system-misc

# GDB
sudo apt install gdb-multiarch

# 验证
riscv64-unknown-elf-gcc --version
qemu-system-riscv64 --version
gdb-multiarch --version
```

> 如果 `riscv64-unknown-elf-gcc` 不可用，可以使用 `riscv64-linux-gnu-gcc`。
> 两者在裸机开发中都能工作，只需在 Makefile 中调整 `CROSS_COMPILE` 前缀。

---

## 2. 了解 QEMU virt 平台

QEMU 的 `virt` 机器的硬件布局：

```
地址范围                    设备
─────────────────────────────────────────
0x0010_0000               QEMU 测试设备（可用于退出 QEMU）
0x0200_0000 - 0x0200_FFFF CLINT（定时器 + 软中断）
0x0C00_0000 - 0x0FFF_FFFF PLIC（外部中断控制器）
0x1000_0000 - 0x1000_0FFF UART0（NS16550A 串口）
0x1000_1000 - ...         VirtIO 设备
0x8000_0000 - ...         RAM（主内存，默认 128MB）
```

---

## 3. 完善链接脚本 `linker.ld`

 `linker.ld` 是控制内存分布的文件，需要提前进行配置
```linker
OUTPUT_ARCH(riscv)
ENTRY(_entry)

BASE_ADDRESS = 0x80000000;

SECTIONS {
    . = BASE_ADDRESS;

    .text : {
        PROVIDE(_text_start = .);
        *(.text .text.*)
        PROVIDE(_text_end = .);
    }

    . = ALIGN(0x1000);

    .rodata : {
        PROVIDE(_rodata_start = .);
        *(.rodata .rodata.*)
        PROVIDE(_rodata_end = .);
    }

    . = ALIGN(0x1000);

    .data : {
        PROVIDE(_data_start = .);
        *(.data .data.*)
        PROVIDE(_data_end = .);
    }

    .bss : {
        PROVIDE(_bss_start = .);
        *(.bss .bss.*)
        *(COMMON)
        PROVIDE(_bss_end = .);
    }

    PROVIDE(_kernel_end = .);
}
```

 `PROVIDE` 符号说明：
- `_text_start` / `_text_end`：代码段的范围，用于设置页表时标记为只读+可执行
- `_rodata_start` / `_rodata_end`：只读数据段范围
- `_data_start` / `_data_end`：数据段范围，标记为可读写
- 后续启用虚拟内存时，不同段需要不同的页权限



---

## 4. 编写 Makefile

项目有子目录结构（`bootloader/`、`kernel/`、`user/` 等），Makefile 需要能处理多目录。

### 4.1 Makefile 框架

```makefile
CROSS_COMPILE = riscv64-unknown-elf-
CC      = $(CROSS_COMPILE)gcc
AS      = $(CROSS_COMPILE)as
LD      = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
MKDIR   = mkdir -p
RM      = rm -rf

# --- 编译选项 ---
# 注意：RV64 使用 rv64imac + lp64（没有 FPU 时）
# 如果需要浮点，用 rv64gc + lp64d
CFLAGS  = -nostdlib -nostartfiles -ffreestanding -fno-builtin
CFLAGS += -Wall -g -O0
CFLAGS += -march=rv64imac -mabi=lp64 -mcmodel=medany

# mcmodel=medany: 允许代码和数据在任意地址，RV64 裸机开发必须加

# --- 输出目录 ---
OUTPUT = out

# --- 源文件 ---
# 阶段 0-1：bootloader + UART 驱动
# 后续阶段逐步添加 kernel/、user/ 等目录的文件
SRCS_ASM = bootloader/entry.S
SRCS_C   = bootloader/main.c \
           kernel/driver/uart.c

# 生成目标文件路径（保持目录结构）
OBJS_ASM = $(addprefix $(OUTPUT)/, $(patsubst %.S, %.o, $(SRCS_ASM)))
OBJS_C   = $(addprefix $(OUTPUT)/, $(patsubst %.c, %.o, $(SRCS_C)))
OBJS     = $(OBJS_ASM) $(OBJS_C)

# --- 输出文件 ---
ELF = $(OUTPUT)/os.elf
BIN = $(OUTPUT)/os.bin

# --- 链接选项 ---
LDFLAGS = -T linker.ld

# --- QEMU ---
QEMU   = qemu-system-riscv64
QFLAGS = -nographic -smp 1 -machine virt -bios none -m 128M

# --- GDB ---
GDB = gdb-multiarch

# ========== 规则 ==========

.DEFAULT_GOAL := all
all: $(ELF)

# 链接
$(ELF): $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)
	$(OBJCOPY) -O binary $@ $(BIN)

# 编译 C 文件
# 注意：自动创建输出子目录
$(OUTPUT)/%.o: %.c
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译汇编文件
$(OUTPUT)/%.o: %.S
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# 运行
run: all
	@echo "Press Ctrl-A then X to exit QEMU"
	@echo "----------------------------------"
	$(QEMU) $(QFLAGS) -kernel $(ELF)

# 调试
.PHONY: debug
debug: all
	@echo "Press Ctrl-C then type 'quit' to exit GDB and QEMU"
	@echo "---------------------------------------------------"
	$(QEMU) $(QFLAGS) -kernel $(ELF) -s -S &
	$(GDB) $(ELF) -q -ex "target remote :1234"

# 反汇编
.PHONY: code
code: all
	$(OBJDUMP) -S $(ELF) | less

# 清理
.PHONY: clean
clean:
	$(RM) $(OUTPUT)
```

### 4.2 关键说明

**`-mcmodel=medany`**：
这是 RV64 裸机开发**必须**加的选项。你之前的 RV32 项目不需要，因为 RV32 地址空间只有 4GB。RV64 的默认代码模型 (`medlow`) 假设所有符号在低 2GB 内，但内核加载在 `0x80000000`，超出了这个范围。`medany` 允许代码在任意 2GB 窗口内。

**目录结构保持**：
规则 `$(OUTPUT)/%.o: %.c` 配合 `@$(MKDIR) $(dir $@)` 会自动在 `out/` 下创建对应的子目录结构：
```
out/
├── bootloader/
│   ├── entry.o
│   └── main.o
├── kernel/
│   └── driver/
│       └── uart.o
├── os.elf
└── os.bin
```

**逐步添加源文件**：
随着开发推进，在 `SRCS_ASM` 和 `SRCS_C` 中添加新文件即可。例如进入阶段 2 后：
```makefile
SRCS_ASM += bootloader/trap_entry.S
SRCS_C   += bootloader/trap.c \
            bootloader/sbi.c
```

进入阶段 3 后添加内核文件：
```makefile
SRCS_C += kernel/main.c \
          kernel/trap/trap.c
SRCS_ASM += kernel/trap/trap_entry.S
```

### 4.3 头文件搜索路径

当项目变大后，你会需要添加 `-I` 选项让编译器找到头文件：

```makefile
CFLAGS += -I include
CFLAGS += -I kernel/lib
CFLAGS += -I kernel/driver
# 根据需要添加更多
```

---

## 5. 测试：编译并运行

### 5.1 创建最小 main.c

现在 `entry.S` 中还没有跳转到 `main`，先验证编译流程能走通。

创建一个空的 `bootloader/main.c`：
```c
void main(void) {
    while (1) {}
}
```

以及空的 `kernel/driver/uart.c`（或者先从 SRCS_C 中去掉）。

### 5.2 编译

```bash
make
```

应该能成功生成 `out/os.elf` 和 `out/os.bin`。

### 5.3 运行

```bash
make run
```

QEMU 应该启动但无输出（因为还没有 UART 驱动）。按 `Ctrl-A` 然后 `X` 退出。

### 5.4 调试验证

```bash
make debug
```

在 GDB 中：
```
(gdb) break _entry
(gdb) continue
(gdb) info registers
(gdb) si
```

确认代码确实在 `0x80000000` 处执行。

---

## 检查清单

- [ ] Ubuntu 上安装了 `riscv64-unknown-elf-gcc`（或等效工具链）
- [ ] Ubuntu 上安装了 `qemu-system-riscv64`
- [ ] Ubuntu 上安装了 `gdb-multiarch`
- [ ] `linker.ld` 添加了各段的 PROVIDE 符号
- [ ] `Makefile` 编写完成，使用 RV64 选项（`rv64imac` + `lp64` + `mcmodel=medany`）
- [ ] `make` 能成功编译生成 `out/os.elf`
- [ ] `make run` 能启动 QEMU（即使没有输出）
- [ ] （可选）`make debug` 能用 GDB 连接并单步调试

---

## 下一步

完成环境搭建后，进入 [阶段 1：启动流程与串口输出](01-boot-and-uart.md)。
