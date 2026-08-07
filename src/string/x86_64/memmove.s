.global memmove
.type memmove,@function
memmove:

# Leaf function that never touches %rsp: the default unwind rules
# (CFA = %rsp + 8, return address at CFA - 8) hold at every instruction, so
# this bare .cfi_startproc emits complete unwind info. The tail jump into
# __memcpy_fwd is covered by the FDE that memcpy.s emits for its own bytes.
.cfi_startproc
	mov %rdi,%rax
	sub %rsi,%rax
	cmp %rdx,%rax
.hidden __memcpy_fwd
	jae __memcpy_fwd
	mov %rdx,%rcx
	lea -1(%rdi,%rdx),%rdi
	lea -1(%rsi,%rdx),%rsi
	std
	rep movsb
	cld
	lea 1(%rdi),%rax
	ret
.cfi_endproc
