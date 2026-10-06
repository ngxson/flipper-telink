OUT_DIR += /platform \
/platform/boot \
/platform/tc32

# irq_handler.c of the SDK is the Zigbee/BLE concurrent one: this BLE-only
# build has its own in src/irq.c
OBJS += \
$(OUT_PATH)/platform/boot/link_cfg.o \
$(OUT_PATH)/platform/tc32/div_mod.o

# Each subdirectory must supply rules for building sources it contributes
$(OUT_PATH)/platform/%.o: $(SDK_PATH)/platform/%.c
	@echo 'Building file: $<'
	@$(TC32_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o"$@" "$<"

$(OUT_PATH)/platform/%.o: $(SDK_PATH)/platform/%.S
	@echo 'Building file: $<'
	@$(TC32_PATH)tc32-elf-gcc $(GCC_FLAGS) $(ASM_FLAGS) $(INCLUDE_PATHS) -c -o"$@" "$<"
