.text
.global __clone
.hidden __clone
.type   __clone,@function
__clone:
# The parent never adjusts rsp, so the default CFI rules hold for it. In the
# child, the return address is marked undefined: that is the outermost frame,
# and it tells the unwinder that the stack ends here. Without an unwind table
# entry, libunwind cannot tell the end of the stack from missing unwind info,
# and it tries to recognize a sigreturn trampoline first, which costs an
# rt_sigprocmask syscall - on every sample of a sampling profiler.
.cfi_startproc
	xor %eax,%eax
	mov $56,%al
	mov %rdi,%r11
	mov %rdx,%rdi
	mov %r8,%rdx
	mov %r9,%r8
	mov 8(%rsp),%r10
	mov %r11,%r9
	and $-16,%rsi
	sub $8,%rsi
	mov %rcx,(%rsi)
	syscall
	test %eax,%eax
	jnz 1f
.cfi_remember_state
.cfi_undefined %rip
	xor %ebp,%ebp
	pop %rdi
	call *%r9
	mov %eax,%edi
	xor %eax,%eax
	mov $60,%al
	syscall
	hlt
.cfi_restore_state
1:	ret
.cfi_endproc
.size __clone,.-__clone
