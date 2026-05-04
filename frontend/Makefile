APP_DIR := app
THEME_DIR := themes
SCRIPT_DIR := scripts
OUT := output
PACKAGE := $(OUT)/unifrog-frontend
MQUICKJS_DIR ?= ../.deps/mquickjs
MQUICKJS_ATOM_ALIGN ?= 256
MQUICKJS_TCC_PATCH ?= ../patches/mquickjs/0001-use-portable-int-overflow-checks.patch
HOSTCC ?= cc
HOST_NO_PIE := $(if $(findstring tcc,$(notdir $(HOSTCC))),,-no-pie)
MQJS_BUILD := $(OUT)/mqjs-build
MQJS := $(MQJS_BUILD)/mqjs
APP_FILES := \
	$(APP_DIR)/theme.js \
	$(APP_DIR)/constants.js \
	$(APP_DIR)/catalog.js \
	$(APP_DIR)/app.js
THEME_FILES := $(shell find $(THEME_DIR) -type f 2>/dev/null)
JS_FILES := main.js $(APP_FILES) \
	$(filter %.js,$(THEME_FILES)) $(wildcard $(SCRIPT_DIR)/*.js)
JS_BYTECODE := $(patsubst %.js,$(OUT)/lint/%.mqbc,$(JS_FILES))
JS_COMPAT_STAMP := $(OUT)/lint/js-compat.stamp
JS_FORBIDDEN_PATTERN := \.(substr|substring|slice|toLowerCase|toUpperCase|trim|startsWith|endsWith|includes|map|filter|reduce|forEach|find|some|every)[[:space:]]*\(

.PHONY: all help check package clean print-config

all: package

help:
	@echo "UniFrog frontend:"
	@echo "  make -C frontend check      Compile shipped JS with MQuickJS"
	@echo "  make -C frontend package    Stage frontend files under frontend/output"
	@echo "  make -C frontend clean      Remove frontend/output"
	@echo "  make -C frontend print-config"

print-config:
	@echo "MQUICKJS_DIR=$(MQUICKJS_DIR)"
	@echo "OUT=$(OUT)"
	@echo "PACKAGE=$(PACKAGE)"
	@echo "HOSTCC=$(HOSTCC)"

check: $(JS_BYTECODE) $(JS_COMPAT_STAMP)
	@test -f main.js
	@test -f $(APP_DIR)/app.js
	@test -f $(APP_DIR)/theme.js
	@test -f $(APP_DIR)/constants.js
	@test -f $(APP_DIR)/catalog.js
	@test -f $(THEME_DIR)/default.ini
	@echo "OK"

package: check $(THEME_FILES)
	@rm -rf $(PACKAGE)
	@mkdir -p $(PACKAGE)
	@cp main.js $(PACKAGE)/
	@mkdir -p $(PACKAGE)/$(APP_DIR) $(PACKAGE)/$(THEME_DIR) $(PACKAGE)/$(SCRIPT_DIR)
	@cp $(APP_FILES) $(PACKAGE)/$(APP_DIR)/
	@cp -R $(THEME_DIR)/. $(PACKAGE)/$(THEME_DIR)/
	@cp $(SCRIPT_DIR)/*.js $(PACKAGE)/$(SCRIPT_DIR)/

clean:
	@rm -rf $(OUT)

$(MQJS):
	@test -f "$(MQUICKJS_DIR)/mquickjs.c" || { echo "missing MQuickJS checkout: $(MQUICKJS_DIR)"; exit 1; }
	@rm -rf $(MQJS_BUILD)
	@mkdir -p $(MQJS_BUILD)
	@cp "$(MQUICKJS_DIR)"/*.[ch] "$(MQJS_BUILD)"/
	@cp "$(MQUICKJS_DIR)/Makefile" "$(MQJS_BUILD)"/
	@if test -f "$(MQUICKJS_TCC_PATCH)" && command -v patch >/dev/null 2>&1; then \
		patch -d "$(MQJS_BUILD)" -p1 < "$(MQUICKJS_TCC_PATCH)"; \
	fi
	@if grep -q '^#define ATOM_ALIGN ' "$(MQJS_BUILD)/mquickjs_build.c"; then \
		sed -i 's/^#define ATOM_ALIGN .*/#define ATOM_ALIGN $(MQUICKJS_ATOM_ALIGN)/' "$(MQJS_BUILD)/mquickjs_build.c"; \
	fi
	@if ! grep -q '^#define ATOM_ALIGN $(MQUICKJS_ATOM_ALIGN)$$' "$(MQJS_BUILD)/mquickjs_build.c"; then \
		echo "error: frontend requires MQuickJS ATOM_ALIGN $(MQUICKJS_ATOM_ALIGN)"; exit 1; \
	fi
	@$(MAKE) -C $(MQJS_BUILD) mqjs CC="$(HOSTCC) $(HOST_NO_PIE)" HOST_CC="$(HOSTCC) $(HOST_NO_PIE)" >/dev/null

$(OUT)/lint/%.mqbc: %.js $(MQJS)
	@mkdir -p $(@D)
	@rm -f $@
	@$(MQJS) -o $@ -m32 $<
	@test -s $@ || { echo "MQuickJS syntax check failed: $<"; exit 1; }

$(JS_COMPAT_STAMP): $(JS_FILES)
	@mkdir -p $(@D)
	@if command -v rg >/dev/null 2>&1; then \
		if rg -n '$(JS_FORBIDDEN_PATTERN)' $(JS_FILES); then \
			echo "Unsupported MQuickJS prototype helper used"; exit 1; \
		fi; \
	else \
		if grep -En '$(JS_FORBIDDEN_PATTERN)' $(JS_FILES); then \
			echo "Unsupported MQuickJS prototype helper used"; exit 1; \
		fi; \
	fi
	@touch $@
