.text
.global __cp_begin
.hidden __cp_begin
.global __cp_end
.hidden __cp_end
.global __cp_cancel
.hidden __cp_cancel
.hidden __cancel
.global __syscall_cp_asm
.hidden __syscall_cp_asm
.type   __syscall_cp_asm,@function
__syscall_cp_asm:
# The default CFI rules (CFA = rsp+8, return address at CFA-8) hold
# throughout: rsp is never adjusted. Without an unwind table entry, a stack
# trace taken while a thread is blocked in the syscall - the query profiler
# signal handler does exactly that for every cancellable syscall, e.g.
# clock_nanosleep under sleep() - falls apart right after this frame and
# never reaches the calling application code.
.cfi_startproc

__cp_begin:
	mov (%rdi),%eax
	test %eax,%eax
	jnz __cp_cancel
	mov %rdi,%r11
	mov %rsi,%rax
	mov %rdx,%rdi
	mov %rcx,%rsi
	mov %r8,%rdx
	mov %r9,%r10
	mov 8(%rsp),%r8
	mov 16(%rsp),%r9
	mov %r11,8(%rsp)
	syscall
__cp_end:
	ret
__cp_cancel:
	jmp __cancel
.cfi_endproc
