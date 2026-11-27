.global vfork
.type vfork,%function
vfork:
	hint #34 // bti c
	mov x8, 220    // SYS_clone
	mov x0, 0x4111 // SIGCHLD | CLONE_VM | CLONE_VFORK
	mov x1, 0
	svc 0
	.hidden __syscall_ret
	b __syscall_ret
.size vfork,.-vfork

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
