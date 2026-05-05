ENTRY(_stage1_start)
OUTPUT_FORMAT("elf32-tradlittlemips")

SECTIONS
{
	. = 0x87ff7000;

	.text : {
		KEEP(*(.text.start))
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
		. = ALIGN(16);
	}

	.data : {
		*(.data)
		*(.data.*)
		. = ALIGN(16);
	}

	.bss : {
		__bss_start = .;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(16);
		__bss_end = .;
	}

	/DISCARD/ : {
		*(.MIPS.abiflags)
		*(.reginfo)
		*(.eh_frame*)
		*(.comment)
		*(.note*)
	}
}
