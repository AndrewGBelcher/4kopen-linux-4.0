cmd_arch/arm/mach-sti/pm/pokeloop.o := /starkl/outputs/starkl-b2264-default/buildroot/host/usr/bin/arm-linux-gcc -Wp,-MD,arch/arm/mach-sti/pm/.pokeloop.o.d  -nostdinc -isystem /starkl/toolchain/usr/bin/../lib/gcc/arm-starkl-linux-gnueabihf/4.9.4/include -I/starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include -Iarch/arm/include/generated  -Iinclude -I/starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include/uapi -Iarch/arm/include/generated/uapi -I/starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/include/uapi -Iinclude/generated/uapi -include /starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/include/linux/kconfig.h -D__KERNEL__ -mlittle-endian -Iarch/arm/mach-sti/include  -D__ASSEMBLY__ -mabi=aapcs-linux -mno-thumb-interwork -funwind-tables -marm -D__LINUX_ARM_ARCH__=7 -march=armv7-a  -include asm/unified.h -msoft-float -gdwarf-2           -c -o arch/arm/mach-sti/pm/pokeloop.o arch/arm/mach-sti/pm/pokeloop.S

source_arch/arm/mach-sti/pm/pokeloop.o := arch/arm/mach-sti/pm/pokeloop.S

deps_arch/arm/mach-sti/pm/pokeloop.o := \
  /starkl/outputs/starkl-b2264-default/buildroot/build/linux-3.10.92/arch/arm/include/asm/unified.h \
    $(wildcard include/config/arm/asm/unified.h) \
    $(wildcard include/config/thumb2/kernel.h) \
  arch/arm/mach-sti/pm/poke_table.h \

arch/arm/mach-sti/pm/pokeloop.o: $(deps_arch/arm/mach-sti/pm/pokeloop.o)

$(deps_arch/arm/mach-sti/pm/pokeloop.o):
