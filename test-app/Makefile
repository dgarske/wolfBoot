-include ../.config
-include ../tools/config.mk
-include ../options.mk

TARGET?=none
ARCH?=ARM
MCUXPRESSO_CMSIS?=$(MCUXPRESSO)/CMSIS

ifeq ($(SIGN),RSA2048)
  IMAGE_HEADER_SIZE:=512
endif

ifeq ($(SIGN),RSA4096)
  IMAGE_HEADER_SIZE:=1024
endif
ifeq ($(HASH),SHA256)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha256.o
  CFLAGS+=-D"WOLFBOOT_HASH_SHA256"
endif

ifeq ($(NVM_FLASH_WRITEONCE),1)
  CFLAGS+=-D"NVM_FLASH_WRITEONCE"
endif


ifeq ($(HASH),SHA3_384)
  WOLFCRYPT_OBJS+=./lib/wolfssl/wolfcrypt/src/sha3.o
  CFLAGS+=-D"WOLFBOOT_HASH_SHA3_384"
endif


ifeq ($(TARGET),ti_hercules)
  APP_OBJS:=app_$(TARGET).o ../src/libwolfboot.o
  CFLAGS+=-I"../include"
else
  CFLAGS+=-g -ggdb -Wall -Wstack-usage=1024 -ffreestanding -Wno-unused -DPLATFORM_$(TARGET) -I../include -nostartfiles
  APP_OBJS:=app_$(TARGET).o led.o system.o timer.o ../src/libwolfboot.o
endif


include ../arch.mk

ifeq ($(TZEN),1)
    APP_OBJS+=../hal/$(TARGET)_ns.o
else
    APP_OBJS+=../hal/$(TARGET).o
endif

ifeq ($(ARCH),RISCV)
  APP_OBJS+=startup_riscv.o vector_riscv.o
endif

ifeq ($(ARCH),ARM)
  APP_OBJS+=startup_arm.o
  CFLAGS+=-DSTM32
endif
ifeq ($(ARCH),ARM_BE)
  APP_OBJS+=startup_arm.o
  CFLAGS+=-DSTACK_PAINTING=0
endif

ifeq ($(V),0)
  Q=@
endif

ifeq ($(ENCRYPT),1)
    CFLAGS+=-D"EXT_ENCRYPTED=1"
endif

ENTRY_POINT=`cat .entry-point-address`
LSCRIPT:=../config/target-app.ld
LSCRIPT_TEMPLATE:=$(ARCH).ld
ifeq ($(TARGET),stm32f7)
  LSCRIPT_TEMPLATE=ARM-stm32f7.ld
  CFLAGS+=-DDUALBANK_SWAP
endif

ifeq ($(TARGET),stm32h7)
  LSCRIPT_TEMPLATE=ARM-stm32h7.ld
endif

ifeq ($(TARGET),stm32l5)
  ifeq ($(TZEN),1)
    LSCRIPT_TEMPLATE=ARM-stm32l5-ns.ld
  else
    LSCRIPT_TEMPLATE=ARM-stm32l5.ld
  endif
  CFLAGS+=-mcpu=cortex-m33  
  LDFLAGS+=-mcpu=cortex-m33
endif



ifeq ($(TARGET),ti_hercules)
  LSCRIPT_TEMPLATE=ARM-r5be.ld
  LDFLAGS+=$(LSCRIPT) --map_file=image.map
else
  LDFLAGS+=-T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=image.map
endif

ifeq ($(EXT_FLASH),1)
  CFLAGS+=-D"EXT_FLASH=1" -D"PART_UPDATE_EXT=1"
endif

ifeq ($(SPI_FLASH),1)
  CFLAGS+=-D"SPI_FLASH"
  APP_OBJS+=../hal/spi/spi_drv_$(SPI_TARGET).o ../src/spi_flash.o
endif

ifeq ($(UART_FLASH),1)
  CFLAGS+=-D"UART_FLASH=1"
  APP_OBJS+= ../src/uart_flash.o ../hal/uart/uart_drv_$(UART_TARGET).o
endif

ifeq ($(TARGET),kinetis)
  CFLAGS+= -I$(MCUXPRESSO_DRIVERS)/drivers -I$(MCUXPRESSO_DRIVERS) -DCPU_$(MCUXPRESSO_CPU) -I$(MCUXPRESSO_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1 -DNVM_FLASH_WRITEONCE=1
  APP_OBJS+= $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_flash.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_cache.o \
  $(MCUXPRESSO_DRIVERS)/drivers/fsl_ftfx_controller.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_gpio.o
endif

ifeq ($(TARGET),imx_rt)
  CFLAGS+=-I$(MCUXPRESSO_DRIVERS)/drivers -I$(MCUXPRESSO_DRIVERS) -DCPU_$(MCUXPRESSO_CPU) -I$(MCUXPRESSO_CMSIS)/Include -DDEBUG_CONSOLE_ASSERT_DISABLE=1 -DXIP_EXTERNAL_FLASH=1 \
          -I$(MCUXPRESSO_DRIVERS)/project_template/ -I$(MCUXPRESSO)/boards/evkmimxrt1060/xip/  \
          -I$(MCUXPRESSO_DRIVERS)/utilities/debug_console/ -I$(MCUXPRESSO)/components/serial_manager \
          -I$(MCUXPRESSO)/components/uart/ -I$(MCUXPRESSO_DRIVERS)/utilities/str/ \
		  -I$(MCUXPRESSO)/components/flash/nor \
		  -I$(MCUXPRESSO)/components/flash/nor/flexspi \
          -DPRINTF_ADVANCED_ENABLE=1 -DSCANF_ADVANCED_ENABLE=1 -DSERIAL_PORT_TYPE_UART=1 -DNDEBUG=1
  LDFLAGS+=-mcpu=cortex-m7 -Wall --specs=nosys.specs -fno-common -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -mthumb -mapcs -Xlinker --gc-sections -Xlinker -static -Xlinker -z -Xlinker muldefs -Xlinker -Map=output.map -static -lm -lc -lnosys
  LSCRIPT_TEMPLATE=imx_rt.ld
  APP_OBJS+=$(MCUXPRESSO_DRIVERS)/drivers/fsl_gpio.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_common.o $(MCUXPRESSO_DRIVERS)/drivers/fsl_clock.o \
            imx_rt_clock_config.o $(MCUXPRESSO_DRIVERS)/system_MIMXRT1062.o \
		    $(MCUXPRESSO_DRIVERS)/drivers/fsl_flexspi.o \
            $(MCUXPRESSO_DRIVERS)/utilities/str/fsl_str.o \
            $(MCUXPRESSO)/components/uart/lpuart_adapter.o \
            $(MCUXPRESSO)/components/serial_manager/serial_manager.o \
            $(MCUXPRESSO)/components/lists/generic_list.o \
            $(MCUXPRESSO)/components/serial_manager/serial_port_uart.o \
            $(MCUXPRESSO_DRIVERS)/drivers/fsl_lpuart.o \
			$(MCUXPRESSO)/components/flash/nor/flexspi/fsl_flexspi_nor_flash.o \
            $(MCUXPRESSO_DRIVERS)/utilities/debug_console/fsl_debug_console.o
endif

ifeq ($(TARGET),stm32g0)
  CFLAGS+=-DNVM_FLASH_WRITEONCE=1
endif

ifeq ($(TARGET),hifive1.freedom)
  CFLAGS+=-I$(FREEDOM_E_SDK)/freedom-metal/ -D__METAL_MACHINE_HEADER=\"$(FREEDOM_E_SDK)/bsp/sifive-hifive1/metal.h\"
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/clock.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/led.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/cache.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/cpu.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/gpio.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/interrupt.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/uart.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/tty.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/spi.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/shutdown.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/timer.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,hfrosc.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,hfxosc.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,pll.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,fe310-g000,prci.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,spi0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,uart0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,global-external-interrupts0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,local-external-interrupts0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,gpio0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/sifive,gpio-leds.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,clint0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,plic0.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/riscv,cpu.o
  APP_OBJS+=$(FREEDOM_E_SDK)/freedom-metal/src/drivers/fixed-clock.o
endif

ifeq ($(TARGET),hifive1)
  CFLAGS+=-DRAMFUNCTION='__attribute__((used,section(".ramcode")))'
  APP_OBJS+=hifive1_write_page.o
endif

# $(CYPRESS_PDL)/devices/templates/COMPONENT_MTB/COMPONENT_CM0P/system_psoc6_cm0plus.o

ifeq ($(TARGET),psoc6)
  LSCRIPT_TEMPLATE:=ARM-psoc6.ld
  APP_OBJS+= $(CYPRESS_PDL)/drivers/source/cy_gpio.o
  APP_OBJS+= $(CYPRESS_PDL)/drivers/source/cy_device.o
  CFLAGS+=-I$(CYPRESS_PDL)/drivers/include/
  CFLAGS+=-I$(CYPRESS_PDL)/devices/include
  CFLAGS+=-I$(CYPRESS_PDL)/cmsis/include
  CFLAGS+=-I$(CYPRESS_TARGET_LIB)
  CFLAGS+=-I$(CYPRESS_CORE_LIB)/include
  CFLAGS+=-I$(CYPRESS_PDL)/devices/include/ip
  CFLAGS+=-I$(CYPRESS_PDL)/devices/templates/COMPONENT_MTB
  CFLAGS+=-DCY8C624ABZI_D44
endif

standalone:CFLAGS+=-D"TEST_APP_STANDALONE"
standalone:LDFLAGS:=-T standalone.ld -Wl,-gc-sections -Wl,-Map=image.map

image.bin: image.elf
	$(Q)echo "\t[BIN] $@"
	$(Q)$(OBJCOPY) -O binary $^ $@

image.elf: $(APP_OBJS) $(LSCRIPT)
	$(Q)echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) $(APP_OBJS) $(OUTPUT_FLAG) $@


standalone: image.bin

../src/libwolfboot.o: ../src/libwolfboot.c FORCE
	$(Q)echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ ../src/libwolfboot.c

%.o:%.c
	$(Q)echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

%.o:%.S
	$(Q)echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUTPUT_FLAG) $@ $^

clean:
	$(Q)rm -f *.bin *.elf tags *.o $(LSCRIPT)

$(LSCRIPT): $(LSCRIPT_TEMPLATE) FORCE
	$(Q)printf "%d" $(WOLFBOOT_PARTITION_BOOT_ADDRESS) > .wolfboot-offset
	$(Q)printf "%d" $(WOLFBOOT_PARTITION_SIZE) > .partition-size
	$(Q)printf "%d" $(IMAGE_HEADER_SIZE) > .header-size
	$(Q)expr `cat .wolfboot-offset` + `cat .header-size` > .entry-point
	$(Q)printf "0x%X" `cat .entry-point` > .entry-point
	$(Q)expr `cat .partition-size` - `cat .header-size` > .app-size
	$(Q)printf "0x%X" `cat .app-size` > .app-size
	$(Q)cat $(LSCRIPT_TEMPLATE) | \
		sed -e "s/##WOLFBOOT_TEST_APP_SIZE##/`cat .app-size`/g" | \
		sed -e "s/##WOLFBOOT_TEST_APP_ADDRESS##/`cat .entry-point`/g" \
		> $(@)
	$(Q)rm -f .app-size .entry-point .wolfboot-offset .partition-size .header-size

FORCE:

.PHONY: FORCE clean
