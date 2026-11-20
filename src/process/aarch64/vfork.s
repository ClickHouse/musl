.global vfork
.type vfork,%function
vfork:
	mov x8, 220    // SYS_clone
	mov x0, 0x4111 // SIGCHLD | CLONE_VM | CLONE_VFORK
	mov x1, 0
	svc 0
	.hidden __fdmap_vfork_ret
	b __fdmap_vfork_ret
.size vfork,.-vfork
