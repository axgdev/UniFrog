ENTRY(_start)
OUTPUT_FORMAT("elf32-tradlittlemips")
SEARCH_DIR(.)
__DYNAMIC  =  0;

/*
 * Allocate the stack to be at the top of memory, since the stack
 * grows down
 */
PROVIDE (__stack = 0);
PROVIDE (hardware_init_hook = 0);
PROVIDE (software_init_hook = 0);

SECTIONS
{
 	. = __RAM_BASE;

	.text : {
		__text_start = .;
		*(.boot)
		*(.text)
		*(.text.*)
		*(.int_head)
		*(.gnu.linkonce.t*)
		*(.mips16.fn.*)
		*(.mips16.call.*)
		*(.MIPS.abiflags)
		PROVIDE (__runtime_reloc_start = .);
		*(.rel.sdata)
		PROVIDE (__runtime_reloc_stop = .);
		*(.fini)
		*(.reuse)
		__CTOR_LIST__ = . ;
		LONG((__CTOR_END__ - __CTOR_LIST__) / 4 - 2)
		KEEP(*(SORT(.ctors)))
		LONG(0)
		__CTOR_END__ = . ;
		__DTOR_LIST__ = . ;
		LONG((__DTOR_END__ - __DTOR_LIST__) / 4 - 2)
		KEEP(*(SORT(.dtors)))
		LONG(0)
		__DTOR_END__ = . ;
		. = ALIGN(16);
		__text_end = .;
	}

	.eh_frame_hdr : {
		*(.eh_frame_hdr)
		*(.eh_frame_entry)
		*(.eh_frame_entry.*)
		. = ALIGN(4);
	}

	.rodata : {
		__rodata_start = .;
		*(.rdata)
		*(.rodata)
		*(.rodata.*)
		*(.gnu.linkonce.r*)
		*(.flash)
		. = ALIGN(4);
		__rodata_end = .;
	}

	.data : {
		__data_start = .;
		*(.eh_frame*)
		*(.data)
		*(.data.*)
		*(.gnu.linkonce.d*)
		*(.gcc_except_table)
		*(.gcc_except_table.*)
		*(.rel.dyn)
		*(.gnu_extab*)
		*(.got*)
		. = ALIGN(4);
		__data_end = .;
	}

	.stack_chk_guard : {
		. = ALIGN(16);
		*(.stack_chk_guard)
	}

	_gp = ALIGN(16) + 0x7FF0 ;

	.lit8 : {
		*(.lit8)
	}

	.lit4 : {
		*(.lit4)
	}

	.standby : {
		. = ALIGN(32);
		__STANDBY_TEXT_START = .;
		KEEP(*(.standby.text))
		__STANDBY_TEXT_END = .;
		. = ALIGN(4);

		__STANDBY_DATA_START = .;
		KEEP(*(.standby.data))
		__STANDBY_DATA_END = .;
		. = ALIGN(4);

		__STANDBY_BSS_START = .;
		KEEP(*(.standby.bss))
		__STANDBY_BSS_END = .;
		. = ALIGN(4);
	}

	.structure.init : {
		_init_start = .;
		_module_init_start = .;
		_module_init_core_start = .;
		KEEP(*(.initcall.core0.init))
		KEEP(*(.initcall.core1.init))
		KEEP(*(.initcall.core2.init))
		KEEP(*(.initcall.core3.init))
		KEEP(*(.initcall.core4.init))
		_module_init_core_end = .;
		_module_init_postcore_start = .;
		KEEP(*(.initcall.postcore0.init))
		KEEP(*(.initcall.postcore1.init))
		KEEP(*(.initcall.postcore2.init))
		KEEP(*(.initcall.postcore3.init))
		KEEP(*(.initcall.postcore4.init))
		_module_init_postcore_end = .;
		_module_init_arch_start = .;
		KEEP(*(.initcall.arch0.init))
		KEEP(*(.initcall.arch1.init))
		KEEP(*(.initcall.arch2.init))
		KEEP(*(.initcall.arch3.init))
		KEEP(*(.initcall.arch4.init))
		_module_init_arch_end = .;
		_module_init_system_start = .;
		KEEP(*(.initcall.system0.init))
		KEEP(*(.initcall.system1.init))
		KEEP(*(.initcall.system2.init))
		KEEP(*(.initcall.system3.init))
		KEEP(*(.initcall.system4.init))
		_module_init_system_end = .;
		_module_init_driver_start = .;
		KEEP(*(.initcall.driver0.init))
		KEEP(*(.initcall.driver1.init))
		KEEP(*(.initcall.driver2.init))
		KEEP(*(.initcall.driver3.init))
		KEEP(*(.initcall.driver4.init))
		_module_init_driver_end = .;
		_module_init_driverlate_start = .;
		KEEP(*(.initcall.driverlate0.init))
		KEEP(*(.initcall.driverlate1.init))
		KEEP(*(.initcall.driverlate2.init))
		KEEP(*(.initcall.driverlate3.init))
		KEEP(*(.initcall.driverlate4.init))
		_module_init_driverlate_end = .;
		_module_init_others_start = .;
		KEEP(*(.initcall.others0.init))
		KEEP(*(.initcall.others1.init))
		KEEP(*(.initcall.others2.init))
		KEEP(*(.initcall.others3.init))
		KEEP(*(.initcall.others4.init))
		_module_init_others_end = .;
		_module_init_end = .;
		. = ALIGN(4);

		_service_init_start = .;
		KEEP(*(.svr_initcall.1.init))
		_service_init_end = .;
		. = ALIGN(4);

		_console_init_start = .;
		KEEP(*(.console.cmd))
		_console_init_end = .;
		. = ALIGN(4);

		_runner_init_start = .;
		KEEP(*(.unity.runner))
		_runner_init_end = .;
		. = ALIGN(4);

		_test_case_init_start = .;
		KEEP(*(.unity.testcase))
		_test_case_init_end = .;
		. = ALIGN(4);

		_padec_start = .;
		KEEP(*(.padec.plugin))
		_padec_end = .;
		. = ALIGN(4);

		_pvdec_start = .;
		KEEP(*(.pvdec.plugin))
		_pvdec_end = .;
		. = ALIGN(4);

		_deca_audio_stream_struct_start = .;
		KEEP(*(.deca.init))
		_deca_audio_stream_struct_end = .;
		_audio_init_struct_start = .;
		KEEP(*(.audio.init))
		_audio_init_struct_end = .;
		. = ALIGN(4);

		_cpustats_start = .;
		KEEP(*(.cpustats.entry))
		_cpustats_end = .;
		. = ALIGN(4);

		_init_end = .;
	}

	.sdata : {
		__sdata_start = .;
		*(.sdata)
		*(.sdata.*)
		*(.gnu.linkonce.s*)
		. = ALIGN(4);
		__sdata_end = .;
	}

	.sbss : {
		__sbss_start = .;
		. = ALIGN(16);
		_sbss = ABSOLUTE(.);
		*(.sbss)
		*(.sbss.*)
		*(.gnu.linkonce.sb.*)
		*(.dynsbss)
		*(.scommon)
		__sbss_end = .;
	}

	.bss : {
		__bss_start = .;
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(.gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(64);
		__bss_end = .;
	}

	. = ALIGN(64);
	.jit (NOLOAD) : {
		__jit_start = .;
		*(.jit)
		*(.jit.*)
		. = ALIGN(64);
		__jit_end = .;
	}
	_ebss = ABSOLUTE(.);

	.reginfo 0 : {
		*(.reginfo)
	}

	.mdebug 0 : {
		*(.mdebug)
	}
}
