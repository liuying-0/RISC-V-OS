# 阶段 0：环境搭建与项目配置

## 目标

完成本阶段后，你将拥有：
- 可用的 RISC-V 64 位交叉编译工具链（Ubuntu 上）
- 可用的 QEMU RISC-V 64 位模拟器（Ubuntu 上）
- 完整的 `Makefile` 和 `linker.ld`
- 一个能编译出内核镜像并在 QEMU 中运行的项目骨架

> **开发环境说明**：本项目在 Mac 上编辑代码，文件存储在 Ubuntu 上，编译和运行都在 Ubuntu 上完成。
> 下面的工具安装步骤针对 Ubuntu。

**实现顺序参考**：各阶段文件的创建时机和内容演进见 [实现顺序与文件规划](00-implementation-order.md)，可避免依赖顺序问题（如 `memlayout.h` 在阶段 3 即需创建初版）。

---

## 1. 与之前 RV32 项目的差异

你之前做过 RV32 的 OS 项目，这次升级到 RV64，主要变化如下：

| 项目 | RV32（旧） | RV64（新） |
|------|-----------|-----------|
| 架构 | `rv32g` / `ilp32` | `rv64imac` / `lp64` |
| QEMU | `qemu-system-riscv32` | `qemu-system-riscv64` |
| 寄存器宽度 | 32 位 | 64 位 |
| 指针大小 | 4 字节 | 8 字节 |
| 汇编存取指令 | `lw` / `sw`（4 字节） | `ld` / `sd`（8 字节） |
| 虚拟内存 | Sv32（2 级页表） | Sv39（3 级页表） |
| 页表项大小 | 4 字节 | 8 字节 |
| 类型定义 | `uint32_t` | `uint64_t`（地址/寄存器相关） |

**特别注意**：在汇编中保存/恢复寄存器时，必须使用 `sd`/`ld`（8 字节），而不是之前的 `sw`/`lw`。栈帧中每个寄存器槽位占 8 字节。

---

## 2. 安装工具链（Ubuntu）

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

## 3. 了解 QEMU virt 平台

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

**注意**：这个布局和你之前 RV32 virt 平台基本一致，地址没有变化。

---

## 4. 完善链接脚本 `linker.ld`

你当前的 `linker.ld` 已经有了基本结构，还需要补充一些符号导出，后续阶段会用到：

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

新增的 `PROVIDE` 符号说明：
- `_text_start` / `_text_end`：代码段的范围，用于设置页表时标记为只读+可执行
- `_rodata_start` / `_rodata_end`：只读数据段范围
- `_data_start` / `_data_end`：数据段范围，标记为可读写
- 后续启用虚拟内存时，不同段需要不同的页权限

> **进阶技巧**：你之前的项目用 `gcc -E -P` 预处理 linker script，这个技巧在新项目中同样可以用。
> 比如用 `#define` 定义 `BASE_ADDRESS`，然后在 C 头文件和 linker script 中共享。
> 可以暂时先不做，等需要时再加。

---

## 5. 编写 Makefile

你的新项目有子目录结构（`bootloader/`、`kernel/`、`user/` 等），Makefile 需要能处理多目录。基于你之前的 Makefile 风格，这里给出适配新项目的版本：

### 5.1 需要调整的关键点

| 旧项目 | 新项目 |
|--------|--------|
| `-march=rv32g -mabi=ilp32` | `-march=rv64imac -mabi=lp64` |
| `qemu-system-riscv32` | `qemu-system-riscv64` |
| 扁平目录，手动列出源文件 | 多子目录，需要自动搜索或按目录列出 |
| 所有代码一起编译 | 阶段性添加目录（先只编译 bootloader/） |

### 5.2 Makefile 框架

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

### 5.3 关键说明

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

### 5.4 头文件搜索路径

当项目变大后，你会需要添加 `-I` 选项让编译器找到头文件：

```makefile
CFLAGS += -I include
CFLAGS += -I kernel/lib
CFLAGS += -I kernel/driver
# 根据需要添加更多
```

---

## 6. 创建项目目录结构

在 Ubuntu 上执行：

```bash
cd /path/to/os
mkdir -p bootloader kernel/trap kernel/mm kernel/proc kernel/sync
mkdir -p kernel/driver kernel/lib user/lib include scripts docs
```

你的 `bootloader/entry.S` 已经有了，其余文件在后续阶段中创建。

---

## 7. 测试：编译并运行

### 7.1 创建最小 main.c

现在 `entry.S` 中还没有跳转到 `main`，先验证编译流程能走通。

创建一个空的 `bootloader/main.c`：
```c
void main(void) {
    while (1) {}
}
```

以及空的 `kernel/driver/uart.c`（或者先从 SRCS_C 中去掉）。

### 7.2 编译

```bash
make
```

应该能成功生成 `out/os.elf` 和 `out/os.bin`。

### 7.3 运行

```bash
make run
```

QEMU 应该启动但无输出（因为还没有 UART 驱动）。按 `Ctrl-A` 然后 `X` 退出。

### 7.4 调试验证

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

## 8. 常见问题

### Q: `riscv64-unknown-elf-gcc` 找不到？
A: 检查是否安装了正确的包。Ubuntu 上也可能叫 `riscv64-linux-gnu-gcc`，相应修改 `CROSS_COMPILE`。

### Q: 链接报错 `relocation truncated to fit: R_RISCV_HI20`？
A: 没有加 `-mcmodel=medany`。RV64 必须加这个选项。

### Q: QEMU 启动后直接退出？
A: 检查 `-bios none` 是否加了，检查 ELF 的入口地址是否是 `0x80000000`。可以用 `riscv64-unknown-elf-objdump -h out/os.elf` 查看段地址。

### Q: Mac 上能直接编译运行吗？
A: 如果你在 Mac 上也安装了 RISC-V 工具链和 QEMU（通过 Homebrew），理论上可以。
但工具链前缀可能不同（`riscv64-elf-` 而不是 `riscv64-unknown-elf-`）。
建议保持在 Ubuntu 上编译运行，Mac 上只做编辑。

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
