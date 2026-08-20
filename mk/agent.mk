# Low-noise wrappers for humans and AI agents. The full command log stays on
# disk while the console shows only target, log path, status, and failure clues.

AGENT_TARGET ?= quick-check
AGENT_LOG_DIR ?= $(BUILD)/logs
AGENT_LOG_FILE ?= $(AGENT_LOG_DIR)/agent-$(AGENT_TARGET).log
AGENT_LOG_LATEST ?= $(AGENT_LOG_DIR)/agent.latest
AGENT_FAIL_TAIL ?= 80
AGENT_FAIL_PATTERN ?= error:|warning:|FAIL|failed|missing:|undefined reference|No rule to make target|tracked generated file

.PHONY: agent agent-build agent-check agent-quick-check agent-verify \
	agent-log-tail agent-log-grep

agent:
	@test "$(AGENT_TARGET)" != agent || { echo "AGENT_TARGET cannot be agent"; exit 2; }
	@mkdir -p "$(AGENT_LOG_DIR)"
	@target="$(AGENT_TARGET)"; \
	log="$(AGENT_LOG_FILE)"; \
	tmp="$$log.tmp"; \
	echo "  AGENT   make $$target"; \
	echo "  LOG     $$log"; \
	rm -f "$$tmp"; \
	( \
		printf '# command: make --no-print-directory %s\n' "$$target"; \
		date -u '+# started: %Y-%m-%dT%H:%M:%SZ'; \
		$(MAKE) --no-print-directory $$target; \
		status=$$?; \
		date -u '+# finished: %Y-%m-%dT%H:%M:%SZ'; \
		exit $$status; \
	) >"$$tmp" 2>&1; \
	status=$$?; \
	mv "$$tmp" "$$log"; \
	printf '%s\n' "$$log" > "$(AGENT_LOG_LATEST)"; \
	if test $$status -eq 0; then \
		echo "  OK      $$target"; \
	else \
		echo "  FAIL    $$target status=$$status"; \
		echo "  HINT    rg -n \"error|FAIL\" $$log"; \
		if command -v rg >/dev/null 2>&1; then \
			rg -n -i '$(AGENT_FAIL_PATTERN)' "$$log" | tail -n "$(AGENT_FAIL_TAIL)" || true; \
		else \
			tail -n "$(AGENT_FAIL_TAIL)" "$$log" || true; \
		fi; \
	fi; \
	exit $$status

agent-build:
	@$(MAKE) --no-print-directory agent AGENT_TARGET=all

agent-check:
	@$(MAKE) --no-print-directory agent AGENT_TARGET=check

agent-quick-check:
	@$(MAKE) --no-print-directory agent AGENT_TARGET=quick-check

agent-verify:
	@$(MAKE) --no-print-directory agent AGENT_TARGET=verify

agent-log-tail:
	@if test -f "$(AGENT_LOG_LATEST)"; then \
		tail -n "$(AGENT_FAIL_TAIL)" "$$(cat "$(AGENT_LOG_LATEST)")"; \
	elif test -f "$(AGENT_LOG_FILE)"; then \
		tail -n "$(AGENT_FAIL_TAIL)" "$(AGENT_LOG_FILE)"; \
	else \
		echo "no agent log found"; exit 1; \
	fi

agent-log-grep:
	@test -n "$(PATTERN)" || { echo "usage: make agent-log-grep PATTERN=..."; exit 2; }
	@if test -f "$(AGENT_LOG_LATEST)"; then \
		log="$$(cat "$(AGENT_LOG_LATEST)")"; \
	elif test -f "$(AGENT_LOG_FILE)"; then \
		log="$(AGENT_LOG_FILE)"; \
	else \
		echo "no agent log found"; exit 1; \
	fi; \
	if command -v rg >/dev/null 2>&1; then \
		rg -n "$(PATTERN)" "$$log"; \
	else \
		grep -nE "$(PATTERN)" "$$log"; \
	fi
