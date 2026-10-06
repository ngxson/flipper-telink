OUT_DIR += $(SRC_DIR) \
$(SRC_DIR)/patch_sdk

OBJS += \
$(OUT_PATH)$(SRC_DIR)/patch_sdk/flash.o \
$(OUT_PATH)$(SRC_DIR)/patch_sdk/flash_drv.o \
$(OUT_PATH)$(SRC_DIR)/patch_sdk/adc_drv.o \
$(OUT_PATH)$(SRC_DIR)/patch_sdk/random.o \
$(OUT_PATH)$(SRC_DIR)/patch_sdk/hw_drv.o \
$(OUT_PATH)$(SRC_DIR)/patch_sdk/drv_nv.o \
$(OUT_PATH)$(SRC_DIR)/patch_sdk/cstartup_8258.o \
$(OUT_PATH)$(SRC_DIR)/main.o \
$(OUT_PATH)$(SRC_DIR)/device.o \
$(OUT_PATH)$(SRC_DIR)/app.o \
$(OUT_PATH)$(SRC_DIR)/app_ui.o \
$(OUT_PATH)$(SRC_DIR)/app_pm.o \
$(OUT_PATH)$(SRC_DIR)/app_ble.o \
$(OUT_PATH)$(SRC_DIR)/battery.o \
$(OUT_PATH)$(SRC_DIR)/buzzer.o \
$(OUT_PATH)$(SRC_DIR)/zb_appCb.o \
$(OUT_PATH)$(SRC_DIR)/tuya_cluster.o \
$(OUT_PATH)$(SRC_DIR)/zigbee_ble_switch.o

# Each subdirectory must supply rules for building sources it contributes
$(OUT_PATH)$(SRC_DIR)/%.o: $(PROJECT_PATH)$(SRC_DIR)/%.c
	@echo 'Building file: $<'
	@$(TC32_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o"$@" "$<"

$(OUT_PATH)$(SRC_DIR)/%.o: $(PROJECT_PATH)$(SRC_DIR)/%.S
	@echo 'Building file: $<'
	@$(TC32_PATH)tc32-elf-gcc $(GCC_FLAGS) $(ASM_FLAGS) $(INCLUDE_PATHS) -c -o"$@" "$<"
