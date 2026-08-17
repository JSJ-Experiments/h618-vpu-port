// SPDX-License-Identifier: MIT
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef int (*ve_initialize_fn)(void);
typedef void (*ve_release_fn)(void);

int main(int argc, char **argv)
{
	const char *library = "libVE.so";
	int initialize_requested = argc == 2 && strcmp(argv[1], "--initialize") == 0;
	int ve_abi_probe;
	void *handle;
	const char *error;
	ve_initialize_fn initialize;
	ve_release_fn release;

	if (argc == 3 && strcmp(argv[1], "--load") == 0)
		library = argv[2];
	else if (argc != 1 && !initialize_requested) {
		fprintf(stderr, "usage: %s [--initialize | --load LIBRARY]\n", argv[0]);
		return 64;
	}
	ve_abi_probe = strcmp(library, "libVE.so") == 0;
	handle = dlopen(library, RTLD_NOW | RTLD_GLOBAL);

	if (!handle) {
		fprintf(stderr, "dlopen(%s): %s\n", library, dlerror());
		return 1;
	}
	printf("Android/Bionic loaded %s\n", library);
	if (!ve_abi_probe) {
		dlclose(handle);
		return 0;
	}

	/* dlsym's error state is per-call; discard optional-loader diagnostics. */
	(void)dlerror();
	initialize = (ve_initialize_fn)dlsym(handle, "VeInitialize");
	error = dlerror();
	if (error || !initialize) {
		fprintf(stderr, "libVE ABI incomplete: %s\n", error ? error : "missing symbol");
		dlclose(handle);
		return 2;
	}
	(void)dlerror();
	release = (ve_release_fn)dlsym(handle, "VeRelease");
	error = dlerror();
	if (error || !release) {
		fprintf(stderr, "libVE ABI incomplete: %s\n", error ? error : "missing symbol");
		dlclose(handle);
		return 2;
	}

	if (initialize_requested) {
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
