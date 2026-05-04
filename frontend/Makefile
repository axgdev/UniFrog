APP_DIR := app
THEME_DIR := themes
SCRIPT_DIR := scripts
OUT := output
PACKAGE := $(OUT)/unifrog-frontend
MQUICKJS_DIR ?= ../mquickjs
HOSTCC ?= cc
MQJS_BUILD := $(OUT)/mqjs-build
MQJS := $(MQJS_BUILD)/mqjs
THEME_FILES := $(shell find $(THEME_DIR) -type f 2>/dev/null)
JS_FILES := main.js $(wildcard $(APP_DIR)/*.js) \
	$(filter %.js,$(THEME_FILES)) $(wildcard $(SCRIPT_DIR)/*.js)
JS_BYTECODE := $(patsubst %.js,$(OUT)/lint/%.mqbc,$(JS_FILES))
JS_COMPAT_STAMP := $(OUT)/lint/js-compat.stamp
JS_FORBIDDEN_PATTERN := \.(substr|substring|slice|toLowerCase|toUpperCase|trim|startsWith|endsWith|includes|map|filter|reduce|forEach|find|some|every)[[:space:]]*\(

.PHONY: all check package clean

all: package

check: $(JS_BYTECODE) $(JS_COMPAT_STAMP)
	@test -f main.js
	@test -f $(APP_DIR)/shell.js
	@test -f $(THEME_DIR)/default.js
	@echo "OK"

package: check $(THEME_FILES)
	@mkdir -p $(PACKAGE)
	@cp main.js $(PACKAGE)/
	@mkdir -p $(PACKAGE)/$(APP_DIR) $(PACKAGE)/$(THEME_DIR) $(PACKAGE)/$(SCRIPT_DIR)
	@cp $(APP_DIR)/*.js $(PACKAGE)/$(APP_DIR)/
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
	@if grep -q '^#define ATOM_ALIGN 64$$' "$(MQJS_BUILD)/mquickjs_build.c"; then \
		sed -i 's/^#define ATOM_ALIGN 64$$/#define ATOM_ALIGN 128/' "$(MQJS_BUILD)/mquickjs_build.c"; \
	fi
	@$(MAKE) -C $(MQJS_BUILD) mqjs CC="$(HOSTCC) -no-pie" HOST_CC="$(HOSTCC) -no-pie" >/dev/null

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
