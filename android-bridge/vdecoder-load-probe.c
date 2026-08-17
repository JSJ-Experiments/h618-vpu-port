// SPDX-License-Identifier: MIT
/* Validate that the private Android CedarX decoder closure loads and creates. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef void *(*create_fn)(void);
typedef void (*destroy_fn)(void *);

int main(void)
{
    void *lib = dlopen("libvdecoder.so", RTLD_NOW | RTLD_LOCAL);
    create_fn create;
    destroy_fn destroy;
    void *decoder;
    if (!lib) { fprintf(stderr, "dlopen libvdecoder.so: %s\n", dlerror()); return 1; }
    create = (create_fn)dlsym(lib, "CreateVideoDecoder");
    destroy = (destroy_fn)dlsym(lib, "DestroyVideoDecoder");
    if (!create || !destroy) { fprintf(stderr, "missing decoder API\n"); return 2; }
    decoder = create();
    if (!decoder) { fprintf(stderr, "CreateVideoDecoder failed\n"); return 3; }
    destroy(decoder);
    dlclose(lib);
    puts("Android CedarX decoder load/create OK");
    return 0;
}
