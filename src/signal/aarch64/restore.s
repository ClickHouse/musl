// The nop below is not executed: the kernel enters the restorer at the
// __restore label. It exists so that the address one byte before __restore
// is not covered by the previous function's unwind table entry.
//
// Unwinders look up unwind info for a return address at (address - 1),
// because a normal return address points after its call instruction. The
// restorer's address is stored as the signal handler's return address, but
// nothing calls it - without the padding, (__restore - 1) is the last byte
// of whatever function the linker placed before this one, its FDE matches,
// and the unwinder walks garbage instead of noticing the signal frame. With
// the padding the lookup fails and the unwinder falls back to recognizing
// the sigreturn trampoline by its instructions, which restores the complete
// interrupted register state from the kernel sigframe.
//
// The kernel's own vDSO trampoline does the same, see commit 87676cfca141
// ("arm64: vdso: Disable dwarf unwinding through the sigreturn trampoline").
	nop
.global __restore
.hidden __restore
.type __restore,%function
__restore:
.global __restore_rt
.hidden __restore_rt
.type __restore_rt,%function
__restore_rt:
	mov x8,#139 // SYS_rt_sigreturn
	svc 0
.size __restore,.-__restore
.size __restore_rt,.-__restore_rt

// The BTI property (GNU_PROPERTY_AARCH64_FEATURE_1_AND): the linker enables BTI for an executable
// only if every input object has it. The restorers need no landing pad: a signal handler returns to
// them with `ret`, which BTI does not check. They must not get one either: unwinders (libunwind, gdb)
// recognize a signal frame by these exact two instructions at the return address.
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
