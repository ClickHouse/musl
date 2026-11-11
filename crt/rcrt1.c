#define START "_start"
#define _dlstart_c _start_c
#include "../ldso/dlstart.c"
#include "syscall.h"
#include "atomic.h"
#include <sys/mman.h>

int main();
weak void _init();
weak void _fini();
int __libc_start_main(int (*)(), int, char **,
	void (*)(), void(*)(), void(*)());

/* In a dynamically linked program the dynamic linker makes PT_GNU_RELRO
 * read-only after relocation (reloc_all in ldso/dynlink.c), but a static
 * PIE never runs it: _start_c applies the relocations and comes straight
 * here. Do the same protection here, after relocation and before libc
 * initialization or any constructor runs. Without it, -z relro only marks
 * the range in the ELF file and the GOT and relocated read-only data stay
 * writable. A failure is fatal, as in the dynamic linker. */
static void protect_relro(unsigned char *base, size_t *sp)
{
	size_t i, aux[AUX_CNT];
	int argc = *sp;
	char **argv = (void *)(sp+1);

	for (i=argc+1; argv[i]; i++);
	size_t *auxv = (void *)(argv+i+1);

	for (i=0; i<AUX_CNT; i++) aux[i] = 0;
	for (i=0; auxv[i]; i+=2) if (auxv[i]<AUX_CNT)
		aux[auxv[i]] = auxv[i+1];

	size_t page_size = aux[AT_PAGESZ];
	Phdr *ph = (void *)aux[AT_PHDR];
	if (!ph || !page_size) return;

	for (i=0; i<aux[AT_PHNUM]; i++, ph=(void *)((char *)ph + aux[AT_PHENT])) {
		if (ph->p_type != PT_GNU_RELRO) continue;
		size_t start = ph->p_vaddr & -page_size;
		size_t end = (ph->p_vaddr + ph->p_memsz) & -page_size;
		if (start != end
			&& __syscall(SYS_mprotect, base + start, end - start, PROT_READ))
			a_crash();
	}
}

/* Used: referenced only from the inline assembly in _dlstart_c, which LTO cannot see. */
hidden __attribute__((used)) void __dls2(unsigned char *base, size_t *sp)
{
	protect_relro(base, sp);
	__libc_start_main(main, *sp, (void *)(sp+1), _init, _fini, 0);
}
