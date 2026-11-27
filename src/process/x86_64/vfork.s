.global vfork
.type vfork,@function
vfork:
	pop %rdx
	mov $58,%eax
	syscall
	push %rdx
	mov %rax,%rdi
	.hidden __fdmap_vfork_ret
	jmp __fdmap_vfork_ret
.size vfork,.-vfork
