ENTRY(_start)
OUTPUT_FORMAT("elf32-tradlittlemips")

SECTIONS
{
	. = 0x80001000;

	.text : {
		*(.text.start)
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
		. = ALIGN(16);
	}

	/DISCARD/ : {
		*(.MIPS.abiflags)
		*(.reginfo)
		*(.eh_frame*)
		*(.comment)
		*(.note*)
	}
}
