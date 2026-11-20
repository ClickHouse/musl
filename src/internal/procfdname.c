#include "syscall.h"

void __procfdname(char *buf, unsigned fd)
{
	unsigned i, j;
	/* The names in /proc/self/fd are kernel descriptors. An invalid descriptor gets a
	 * name that does not exist. */
	int kfd = __fdmap_kernel(fd);
	fd = kfd < 0 ? -1 : kfd;
	for (i=0; (buf[i] = "/proc/self/fd/"[i]); i++);
	if (!fd) {
		buf[i] = '0';
		buf[i+1] = 0;
		return;
	}
	for (j=fd; j; j/=10, i++);
	buf[i] = 0;
	for (; fd; fd/=10) buf[--i] = '0' + fd%10;
}
