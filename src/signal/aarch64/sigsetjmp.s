.global sigsetjmp
.global __sigsetjmp
.type sigsetjmp,%function
.type __sigsetjmp,%function
sigsetjmp:
__sigsetjmp:
	cbz x1,setjmp

	// Signed with sp as the modifier like in setjmp.s; sp is the same
	// when control comes back here, whether from setjmp or from longjmp.
	paciasp
	str x30,[x0,#176]
	str x19,[x0,#176+8+8]
	mov x19,x0

	bl setjmp

	mov w1,w0
	mov x0,x19
	ldr x30,[x0,#176]
	autiasp
	ldr x19,[x0,#176+8+8]

.hidden __sigsetjmp_tail
	b __sigsetjmp_tail
