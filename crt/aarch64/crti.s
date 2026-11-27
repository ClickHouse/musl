.section .init
.global _init
.type _init,%function
.align 2
_init:
	hint #34 // bti c
	stp x29,x30,[sp,-16]!
	mov x29,sp

.section .fini
.global _fini
.type _fini,%function
.align 2
_fini:
	hint #34 // bti c
	stp x29,x30,[sp,-16]!
	mov x29,sp

// The BTI property (GNU_PROPERTY_AARCH64_FEATURE_1_AND): the linker enables BTI for an executable
// only if every input object has it. With the `bti c` landing pads at the function entries above,
// this code is valid with BTI enforced; both are no-ops for builds and CPUs without BTI.
.section .note.gnu.property,"a"
.p2align 3
.word 4
.word 16
.word 5
.asciz "GNU"
.word 0xc0000000
.word 4
.word 1
.word 0
