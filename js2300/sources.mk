JS2300_HOST_SOURCES := \
	js2300/src/unifrog_host/frontend.c \
	js2300/src/unifrog_host/actions.c \
	js2300/src/unifrog_host/bindings.c \
	js2300/src/unifrog_host/storage.c

JS2300_HOST_OBJECTS := \
	$(patsubst js2300/src/%.c,$(BUILD)/js2300/%.o,$(JS2300_HOST_SOURCES))
