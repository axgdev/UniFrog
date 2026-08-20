# UniFrog-owned foundation and platform objects. Third-party support objects
# are appended by the root Makefile after their dependency paths are configured.

FOUNDATION_RUNTIME_OBJECTS := \
	$(BUILD)/abi/unifrog_abi.o \
	$(BUILD)/abi/unifrog_abi_tramp.o \
	$(BUILD)/config/unifrog_config.o \
	$(BUILD)/platform/sf2000/audio/unifrog_audio.o \
	$(BUILD)/platform/sf2000/audio/unifrog_audio_gb300_diag.o \
	$(BUILD)/platform/sf2000/audio/unifrog_av.o \
	$(BUILD)/archive/unifrog_zip.o \
	$(BUILD)/archive/unifrog_zip_writer.o \
	$(BUILD)/content/unifrog_reader_content.o \
	$(BUILD)/platform/sf2000/unifrog_backlight.o \
	$(BUILD)/platform/sf2000/unifrog_battery.o \
	$(BUILD)/runtime/unifrog_battery_policy.o \
	$(BUILD)/platform/sf2000/unifrog_boot.o \
	$(BUILD)/runtime/unifrog_boot_policy.o \
	$(BUILD)/runtime/unifrog_boot_logo.o \
	$(BUILD)/runtime/unifrog_fastboot_stage.o \
	$(BUILD)/runtime/unifrog_boot_trace.o \
	$(BUILD)/runtime/unifrog_artwork.o \
	$(BUILD)/runtime/unifrog_bug_report.o \
	$(BUILD)/runtime/unifrog_clock.o \
	$(BUILD)/modules/unifrog_core_module_loader.o \
	$(BUILD)/device/unifrog_device.o \
	$(BUILD)/runtime/unifrog_diag.o \
	$(BUILD)/runtime/unifrog_exception_record.o \
	$(BUILD)/platform/sf2000/display/unifrog_fb.o \
	$(BUILD)/platform/sf2000/display/unifrog_ge.o \
	$(BUILD)/display/unifrog_gfx.o \
	$(BUILD)/display/unifrog_image.o \
	$(BUILD)/platform/sf2000/input/unifrog_input.o \
	$(BUILD)/platform/sf2000/input/unifrog_input_wireless.o \
	$(BUILD)/runtime/unifrog_log.o \
	$(BUILD)/runtime/unifrog_log_policy.o \
	$(BUILD)/abi/unifrog_mips_call.o \
	$(BUILD)/runtime/unifrog_panic.o \
	$(BUILD)/runtime/unifrog_path.o \
	$(BUILD)/platform/sf2000/unifrog_platform.o \
	$(BUILD)/platform/sf2000/unifrog_platform_storage.o \
	$(BUILD)/platform/sf2000/hc_mmc_host.o \
	$(BUILD)/platform/sf2000/hc_mmc_driver.o \
	$(BUILD)/platform/sf2000/hc_mmc_host_swap.o \
	$(BUILD)/platform/sf2000/unifrog_perf.o \
	$(BUILD)/platform/sf2000/unifrog_task.o \
	$(BUILD)/display/unifrog_png.o \
	$(BUILD)/display/unifrog_presenter.o \
	$(BUILD)/runtime/unifrog_runtime.o \
	$(BUILD)/platform/sf2000/unifrog_scpu.o \
	$(BUILD)/storage/unifrog_storage_io.o \
	$(BUILD)/storage/unifrog_storage_profile.o \
	$(BUILD)/display/unifrog_surface_alloc.o \
	$(BUILD)/display/unifrog_text.o \
	$(BUILD)/display/unifrog_ui.o
