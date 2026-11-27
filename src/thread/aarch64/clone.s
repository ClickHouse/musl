// __clone(func, stack, flags, arg, ptid, tls, ctid)
//         x0,   x1,    w2,    x3,  x4,   x5,  x6

// syscall(SYS_clone, flags, stack, ptid, tls, ctid)
//         x8,        x0,    x1,    x2,   x3,  x4

.global __clone
.hidden __clone
.type   __clone,%function
__clone:
	hint #34 // bti c
	// align stack and save func,arg
	and x1,x1,#-16
	stp x0,x3,[x1,#-16]!

	// syscall
	uxtw x0,w2
	mov x2,x4
	mov x3,x5
	mov x4,x6
	mov x8,#220 // SYS_clone
	svc #0

	cbz x0,1f
	// parent
	ret
	// child
1:	mov x29, 0
	ldp x1,x0,[sp],#16
	blr x1
	mov x8,#93 // SYS_exit
	svc #0
.size __clone,.-__clone

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
