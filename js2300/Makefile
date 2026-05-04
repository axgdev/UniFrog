TOOLCHAIN ?= /opt/mipsel-mti-elf
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/mipsel-mti-elf-
BUILD ?= build
OUT ?= output
TARGET := $(OUT)/libjs2300.a
MQUICKJS_DIR ?= libs/mquickjs
MQUICKJS_ATOM_ALIGN_PATCH ?=
MQUICKJS_TCC_PATCH ?= ../patches/mquickjs/0001-use-portable-int-overflow-checks.patch
MQUICKJS_BUILD := $(BUILD)/mquickjs
MQUICKJS_PREPARE_STAMP := $(MQUICKJS_BUILD)/.prepared

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
HOSTCC ?= cc

CFLAGS ?= -EL -march=mips32 -mtune=mips32 -Os -pipe -msoft-float \
	-fsigned-char -G0 -mno-abicalls -fno-pic \
	-ffunction-sections -fdata-sections \
	-Wall -Wextra -Iinclude -I$(MQUICKJS_BUILD) -I$(BUILD) \
	-D_GNU_SOURCE -DCONFIG_SMALL -DUSE_SOFTFLOAT
HOST_CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra -D_GNU_SOURCE -I$(MQUICKJS_BUILD)
MQUICKJS_CFLAGS := $(CFLAGS) -Wno-format -Wno-sign-compare \
	-Wno-unused-parameter -Wno-type-limits -Wno-dangling-pointer

OBJECTS := \
	$(BUILD)/js2300_runtime.o \
	$(BUILD)/mquickjs.o \
	$(BUILD)/dtoa.o \
	$(BUILD)/libm.o \
	$(BUILD)/cutils.o

.PHONY: all clean check

all: $(TARGET)

check: $(TARGET)
	@test -s $(TARGET)
	@echo "OK"

$(BUILD) $(OUT):
	@mkdir -p $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(MQUICKJS_PREPARE_STAMP): $(MQUICKJS_DIR)/mquickjs.c $(MQUICKJS_DIR)/mquickjs_build.c | $(BUILD)
	@rm -rf "$(MQUICKJS_BUILD)"
	@mkdir -p "$(MQUICKJS_BUILD)"
	@cp "$(MQUICKJS_DIR)"/*.[ch] "$(MQUICKJS_BUILD)"/
	@cp "$(MQUICKJS_DIR)/Makefile" "$(MQUICKJS_BUILD)"/
	@if test -f "$(MQUICKJS_TCC_PATCH)" && command -v patch >/dev/null 2>&1; then \
		patch -d "$(MQUICKJS_BUILD)" -p1 < "$(MQUICKJS_TCC_PATCH)"; \
	fi
	@if grep -q '^#define ATOM_ALIGN 64$$' "$(MQUICKJS_BUILD)/mquickjs_build.c"; then \
		if test -n "$(MQUICKJS_ATOM_ALIGN_PATCH)" && test -f "$(MQUICKJS_ATOM_ALIGN_PATCH)" && command -v patch >/dev/null 2>&1; then \
			patch -d "$(MQUICKJS_BUILD)" -p1 < "$(MQUICKJS_ATOM_ALIGN_PATCH)"; \
		else \
			sed -i 's/^#define ATOM_ALIGN 64$$/#define ATOM_ALIGN 128/' "$(MQUICKJS_BUILD)/mquickjs_build.c"; \
		fi; \
	fi
	@if ! grep -q '^#define ATOM_ALIGN 128$$' "$(MQUICKJS_BUILD)/mquickjs_build.c"; then \
		echo "error: JS2300 requires MQuickJS ATOM_ALIGN 128"; exit 1; \
	fi
	@touch $@

$(BUILD)/js2300_stdlib_gen.o: js2300_stdlib_gen.c $(MQUICKJS_PREPARE_STAMP) | $(BUILD)
	@echo "  HOSTCC  $<"
	@$(HOSTCC) $(HOST_CFLAGS) -o $@ -c $<

$(BUILD)/mquickjs_build.host.o: $(MQUICKJS_PREPARE_STAMP) | $(BUILD)
	@echo "  HOSTCC  $(MQUICKJS_BUILD)/mquickjs_build.c"
	@$(HOSTCC) $(HOST_CFLAGS) -o $@ -c $(MQUICKJS_BUILD)/mquickjs_build.c

$(BUILD)/js2300_stdlib_gen: $(BUILD)/js2300_stdlib_gen.o $(BUILD)/mquickjs_build.host.o
	@echo "  HOSTLD  $@"
	@$(HOSTCC) -o $@ $^

$(BUILD)/js2300_stdlib.h: $(BUILD)/js2300_stdlib_gen
	@echo "  GEN     $@"
	@$< -m32 > $@

$(MQUICKJS_BUILD)/mquickjs_atom.h: $(BUILD)/js2300_stdlib_gen $(MQUICKJS_PREPARE_STAMP)
	@echo "  GEN     $@"
	@$< -a -m32 > $@

$(BUILD)/js2300_runtime.o: src/js2300_runtime.c $(BUILD)/js2300_stdlib.h $(MQUICKJS_PREPARE_STAMP) | $(BUILD)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MD -MP -c $< -o $@

$(BUILD)/mquickjs.o: $(MQUICKJS_BUILD)/mquickjs_atom.h | $(BUILD)
	@echo "  CC      $(MQUICKJS_BUILD)/mquickjs.c"
	@$(CC) $(MQUICKJS_CFLAGS) -MD -MP -c $(MQUICKJS_BUILD)/mquickjs.c -o $@

$(BUILD)/dtoa.o: $(MQUICKJS_PREPARE_STAMP) | $(BUILD)
	@echo "  CC      $(MQUICKJS_BUILD)/dtoa.c"
	@$(CC) $(MQUICKJS_CFLAGS) -MD -MP -c $(MQUICKJS_BUILD)/dtoa.c -o $@

$(BUILD)/libm.o: $(MQUICKJS_PREPARE_STAMP) | $(BUILD)
	@echo "  CC      $(MQUICKJS_BUILD)/libm.c"
	@$(CC) $(MQUICKJS_CFLAGS) -MD -MP -c $(MQUICKJS_BUILD)/libm.c -o $@

$(BUILD)/cutils.o: $(MQUICKJS_PREPARE_STAMP) | $(BUILD)
	@echo "  CC      $(MQUICKJS_BUILD)/cutils.c"
	@$(CC) $(MQUICKJS_CFLAGS) -MD -MP -c $(MQUICKJS_BUILD)/cutils.c -o $@

$(TARGET): $(OBJECTS) | $(OUT)
	@echo "  AR      $@"
	@$(AR) rcs $@ $^

clean:
	@rm -rf $(BUILD) $(OUT)

-include $(OBJECTS:.o=.d)
