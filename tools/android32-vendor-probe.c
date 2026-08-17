// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef int (*ve_initialize_fn)(void);
typedef void (*ve_release_fn)(void);

int main(int argc, char **argv)
{
	void *handle = dlopen("libVE.so", RTLD_NOW | RTLD_GLOBAL);
	const char *error;
	ve_initialize_fn initialize;
	ve_release_fn release;

	if (!handle) {
		fprintf(stderr, "dlopen(libVE.so): %s\n", dlerror());
		return 1;
	}
	puts("Android/Bionic loaded libVE.so");

	initialize = (ve_initialize_fn)dlsym(handle, "VeInitialize");
	release = (ve_release_fn)dlsym(handle, "VeRelease");
	error = dlerror();
	if (error || !initialize || !release) {
		fprintf(stderr, "libVE ABI incomplete: %s\n", error ? error : "missing symbol");
		dlclose(handle);
		return 2;
	}

	if (argc == 2 && strcmp(argv[1], "--initialize") == 0) {
		int result = initialize();
		printf("VeInitialize returned %d\n", result);
		if (result == 0)
			release();
		if (dlclose(handle) != 0)
			return 3;
		return result == 0 ? 0 : 4;
	}

	puts("libVE ABI probe passed (use --initialize only with the legacy VE driver active)");
	dlclose(handle);
	return 0;
}
