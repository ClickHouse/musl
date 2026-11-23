.global sigsetjmp
.global __sigsetjmp
.type sigsetjmp,%function
.type __sigsetjmp,%function
sigsetjmp:
__sigsetjmp:
	hint #34 // bti c
	cbz x1,setjmp

	// Signed with sp as the modifier like in setjmp.s; sp is the same
	// when control comes back here, whether from setjmp or from longjmp.
	paciasp
	str x30,[x0,#176]
	str x19,[x0,#176+8+8]
	mov x19,x0

	bl setjmp
	// `longjmp` returns here with `br x30`: with BTI, the return site of `setjmp` needs a `bti j`
	// landing pad, which the compiler inserts after calls to `setjmp` in C but not here.
	hint #36 // bti j

	mov w1,w0
	mov x0,x19
	ldr x30,[x0,#176]
	autiasp
	ldr x19,[x0,#176+8+8]

.hidden __sigsetjmp_tail
	b __sigsetjmp_tail
.size sigsetjmp,.-sigsetjmp
.size __sigsetjmp,.-__sigsetjmp

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
