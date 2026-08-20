MEDIA_PORTABLE_OBJECTS := \
	$(BUILD)/media/unifrog_media_policy.o \
	$(BUILD)/media/unifrog_media_config.o \
	$(BUILD)/media/unifrog_media_content.o

MEDIA_SF2000_OBJECTS := \
	$(BUILD)/media/unifrog_media.o \
	$(BUILD)/media/unifrog_media_buffered_io.o \
	$(BUILD)/media/unifrog_media_direct_audio.o \
	$(BUILD)/media/unifrog_media_gb300_audio_diag.o

MEDIA_SDK_OPTIONAL_STUB_OBJECT := \
	$(BUILD)/runtime/unifrog_sdk_optional_stubs.o

MEDIA_UNIFROG_OBJECTS := $(MEDIA_PORTABLE_OBJECTS)
ifneq ($(filter native firmware,$(HCRTOS_MEDIA)),)
MEDIA_UNIFROG_OBJECTS += $(MEDIA_SF2000_OBJECTS)
else
MEDIA_UNIFROG_OBJECTS += $(MEDIA_SDK_OPTIONAL_STUB_OBJECT)
endif

MEDIA_COMPONENT_OBJECTS := $(filter $(BUILD)/media/%,$(MEDIA_UNIFROG_OBJECTS))

HCRTOS_MEDIA_MODULE_OBJECTS := \
	$(BUILD)/runtime_modules/media/unifrog_media_module_entry.o \
	$(BUILD)/runtime_modules/media/unifrog_media_module_support.o \
	$(BUILD)/runtime_modules/media/unifrog_media_config.o \
	$(BUILD)/runtime_modules/media/unifrog_media_content.o \
	$(BUILD)/runtime_modules/media/unifrog_media.o \
	$(BUILD)/runtime_modules/media/unifrog_media_buffered_io.o \
	$(BUILD)/runtime_modules/media/unifrog_media_direct_audio.o \
	$(BUILD)/runtime_modules/media/unifrog_media_gb300_audio_diag.o
