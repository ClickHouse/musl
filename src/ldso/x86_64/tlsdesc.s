.text
.global __tlsdesc_static
.hidden __tlsdesc_static
.type __tlsdesc_static,@function
__tlsdesc_static:
	mov 8(%rax),%rax
	ret
.size __tlsdesc_static,.-__tlsdesc_static

.global __tlsdesc_dynamic
.hidden __tlsdesc_dynamic
.type __tlsdesc_dynamic,@function
__tlsdesc_dynamic:
	mov 8(%rax),%rax
	push %rdx
	mov %fs:8,%rdx
	push %rcx
	mov (%rax),%rcx
	mov 8(%rax),%rax
	add (%rdx,%rcx,8),%rax
	pop %rcx
	sub %fs:0,%rax
	pop %rdx
	ret
.size __tlsdesc_dynamic,.-__tlsdesc_dynamic
