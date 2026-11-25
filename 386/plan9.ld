/*
 * GCC port: linker script for 386 user programs, the layout 8l
 * produces (as amd64/plan9.ld for 6l): a text segment with the code
 * only, starting at UTZERO (0x1000) with the headers, and a data
 * segment with the initialised data, the constants and string literals
 * (kencc keeps them writable in the data segment), and bss, on the next
 * page (8l -R4096).
 */
ENTRY(_main)

PHDRS
{
	text PT_LOAD FILEHDR PHDRS FLAGS(5);	/* R E */
	data PT_LOAD FLAGS(6);			/* RW */
}

SECTIONS
{
	. = 0x1000 + SIZEOF_HEADERS;
	.text : {
		*(.text .text.*)
	} :text
	/* unwind tables: read-only; for debuggers, and for C++ exceptions
	 * of the native gcc (../9front-gcc): its crtbegin.o registers
	 * .eh_frame, crtend.o ends it; the language-specific data with it */
	.eh_frame : {
		PROVIDE(__eh_frame_start = .);
		KEEP(*(.eh_frame))
		PROVIDE(__eh_frame_end = .);
	} :text
	/* clang's libunwind (toolchain/clang) finds the tables by these
	 * symbols, as on bare metal (no dl_iterate_phdr); the index only
	 * with ld --eh-frame-hdr (clang's driver), empty otherwise */
	.eh_frame_hdr : {
		KEEP(*(.eh_frame_hdr))
	} :text
	PROVIDE(__eh_frame_hdr_start = SIZEOF(.eh_frame_hdr) > 0 ? ADDR(.eh_frame_hdr) : 0);
	PROVIDE(__eh_frame_hdr_end = SIZEOF(.eh_frame_hdr) > 0 ? ADDR(.eh_frame_hdr) + SIZEOF(.eh_frame_hdr) : 0);
	.gcc_except_table : {
		*(.gcc_except_table .gcc_except_table.*)
	} :text
	PROVIDE(etext = .);

	. = ALIGN(0x1000);
	.data : {
		*(.data .data.*)
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	} :data
	/* C++ constructors: 9front's C has none; the native gcc
	 * (../9front-gcc) runs them from __wrap_main before main */
	.init_array : {
		PROVIDE(__init_array_start = .);
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
		KEEP(*(.init_array))
		PROVIDE(__init_array_end = .);
	} :data
	PROVIDE(edata = .);
	/* Go's .noptrbss too (cgo programs of ../9front-go, linked by gcc):
	 * before end, where APE's malloc begins; as an orphan after .bss it
	 * came after end, and malloc's memory was Go's variables */
	.bss : {
		*(.bss .bss.* COMMON)
		*(.noptrbss .noptrbss.*)
	} :data
	PROVIDE(end = .);
	PROVIDE(_end = .);

	/DISCARD/ : {
		*(.note.gnu.property .note.GNU-stack .interp .dynamic)
	}
}
