// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <sys/utsname.h>

int main(void)
{
	struct utsname u;

	if (uname(&u) != 0)
		return 1;
	printf("arm32 compat OK: userspace=%zu-bit kernel=%s %s\n",
	       sizeof(void *) * 8, u.machine, u.release);
	return sizeof(void *) == 4 ? 0 : 2;
}
