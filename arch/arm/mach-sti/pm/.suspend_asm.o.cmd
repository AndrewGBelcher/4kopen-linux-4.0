cmd_arch/arm/mach-sti/pm/suspend_asm.o := /starkl/outputs/starkl-b2264-default/buildroot/host/usr/bin/arm-linux-gcc -Wp,-MD,arch/arm/mach-sti/pm/.suspend_asm.o.d  -nostdinc -isystem /starkl/toolchain/usr/bin/../lib/gcc/arm-starkl-linux-gnueabihf/4.9.4/include -I/starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include -Iarch/arm/include/generated  -Iinclude -I/starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include/uapi -Iarch/arm/include/generated/uapi -I/starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/include/uapi -Iinclude/generated/uapi -include /starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/include/linux/kconfig.h -D__KERNEL__ -mlittle-endian -Iarch/arm/mach-sti/include  -D__ASSEMBLY__ -mabi=aapcs-linux -mno-thumb-interwork -funwind-tables -marm -D__LINUX_ARM_ARCH__=7 -march=armv7-a  -include asm/unified.h -msoft-float -gdwarf-2           -c -o arch/arm/mach-sti/pm/suspend_asm.o arch/arm/mach-sti/pm/suspend_asm.S

source_arch/arm/mach-sti/pm/suspend_asm.o := arch/arm/mach-sti/pm/suspend_asm.S

deps_arch/arm/mach-sti/pm/suspend_asm.o := \
  /starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include/asm/unified.h \
    $(wildcard include/config/arm/asm/unified.h) \
    $(wildcard include/config/thumb2/kernel.h) \
  include/linux/linkage.h \
  include/linux/compiler.h \
    $(wildcard include/config/sparse/rcu/pointer.h) \
    $(wildcard include/config/trace/branch/profiling.h) \
    $(wildcard include/config/profile/all/branches.h) \
    $(wildcard include/config/enable/must/check.h) \
    $(wildcard include/config/enable/warn/deprecated.h) \
    $(wildcard include/config/kprobes.h) \
  include/linux/stringify.h \
  include/linux/export.h \
    $(wildcard include/config/have/underscore/symbol/prefix.h) \
    $(wildcard include/config/modules.h) \
    $(wildcard include/config/modversions.h) \
    $(wildcard include/config/unused/symbols.h) \
  /starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include/asm/linkage.h \
  /starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include/asm/cp15.h \
    $(wildcard include/config/cpu/cp15.h) \
    $(wildcard include/config/smp.h) \
  /starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include/asm/barrier.h \
    $(wildcard include/config/cpu/32v6k.h) \
    $(wildcard include/config/cpu/xsc3.h) \
    $(wildcard include/config/cpu/fa526.h) \
    $(wildcard include/config/arch/has/barriers.h) \
    $(wildcard include/config/arm/dma/mem/bufferable.h) \

arch/arm/mach-sti/pm/suspend_asm.o: $(deps_arch/arm/mach-sti/pm/suspend_asm.o)

$(deps_arch/arm/mach-sti/pm/suspend_asm.o):
