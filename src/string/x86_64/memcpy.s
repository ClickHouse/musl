.global memcpy
.global __memcpy_fwd
.hidden __memcpy_fwd
.type memcpy,@function
memcpy:
__memcpy_fwd:

# Leaf function that never touches %rsp, so the default unwind rules
# (CFA = %rsp + 8, return address at CFA - 8) hold at every instruction and
# this bare .cfi_startproc emits complete unwind info for both entry points.
# Without an FDE, stack traces from profiling signals or faults landing in
# one of the hottest functions lose the entire caller chain.
.cfi_startproc
	mov %rdi,%rax
	cmp $8,%rdx
	jc 1f
	test $7,%edi
	jz 1f
2:	movsb
	dec %rdx
	test $7,%edi
	jnz 2b
1:	mov %rdx,%rcx
	shr $3,%rcx
	rep
	movsq
	and $7,%edx
	jz 1f
2:	movsb
	dec %edx
	jnz 2b
1:	ret
.cfi_endproc
