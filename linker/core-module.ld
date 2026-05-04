ENTRY(unifrog_core_module_entry)
OUTPUT_FORMAT("elf32-tradlittlemips")
SEARCH_DIR(.)
__DYNAMIC = 0;

PROVIDE(__stack = 0);
PROVIDE(hardware_init_hook = 0);
PROVIDE(software_init_hook = 0);

SECTIONS
{
   . = DEFINED(__UNIFROG_MODULE_BASE) ? __UNIFROG_MODULE_BASE : 0x83000000;
   __unifrog_module_start = .;

   .unifrog_module_header : {
      KEEP(*(.unifrog_module_header))
      . = ALIGN(64);
   }

   .text : {
      __text_start = .;
      *(.text.unifrog_core_module_entry)
      *(.text)
      *(.text.*)
      *(.gnu.linkonce.t*)
      *(.mips16.fn.*)
      *(.mips16.call.*)
      *(.MIPS.abiflags)
      *(.fini)
      *(.reuse)
      __CTOR_LIST__ = .;
      LONG((__CTOR_END__ - __CTOR_LIST__) / 4 - 2)
      KEEP(*(SORT(.ctors)))
      LONG(0)
      __CTOR_END__ = .;
      __DTOR_LIST__ = .;
      LONG((__DTOR_END__ - __DTOR_LIST__) / 4 - 2)
      KEEP(*(SORT(.dtors)))
      LONG(0)
      __DTOR_END__ = .;
      __init_array_start = .;
      KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
      KEEP(*(.init_array))
      __init_array_end = .;
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

   _gp = ALIGN(16) + 0x7FF0;

   .lit8 : { *(.lit8) }
   .lit4 : { *(.lit4) }

   .sdata : {
      __sdata_start = .;
      *(.sdata)
      *(.sdata.*)
      *(.gnu.linkonce.s*)
      . = ALIGN(4);
      __sdata_end = .;
   }

   . = ALIGN(64);
   __unifrog_module_file_end = .;

   .sbss (NOLOAD) : {
      __sbss_start = .;
      __unifrog_module_bss_start = .;
      . = ALIGN(16);
      _sbss = ABSOLUTE(.);
      *(.sbss)
      *(.sbss.*)
      *(.gnu.linkonce.sb.*)
      *(.dynsbss)
      *(.scommon)
      __sbss_end = .;
   }

   .bss (NOLOAD) : {
      __bss_start = .;
      *(.dynbss)
      *(.bss)
      *(.bss.*)
      *(.gnu.linkonce.b.*)
      *(COMMON)
      . = ALIGN(64);
      __bss_end = .;
   }

   .jit (NOLOAD) : {
      . = ALIGN(64);
      __jit_start = .;
      *(.jit)
      *(.jit.*)
      . = ALIGN(64);
      __jit_end = .;
   }

   . = ALIGN(64);
   __unifrog_module_bss_end = .;
   __unifrog_module_end = .;
   _ebss = ABSOLUTE(.);

   .reginfo 0 : { *(.reginfo) }
   .mdebug 0 : { *(.mdebug) }

   /DISCARD/ : {
      *(.comment)
      *(.note.GNU-stack)
   }
}
