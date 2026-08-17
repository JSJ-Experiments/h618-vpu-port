// SPDX-License-Identifier: MIT
/* Verify the Android/Bionic replacement's exported ABI before VENC loading. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

struct ScMemOpsS {
    int (*open)(void); void (*close)(void); int (*total_size)(void);
    void *(*palloc)(int, void *, void *); void (*pfree)(void *, void *, void *);
    void (*flush_cache)(void *, int); void *(*ve_get_phyaddr)(void *);
    void *(*ve_get_viraddr)(void *); void *(*cpu_get_phyaddr)(void *);
    void *(*cpu_get_viraddr)(void *); int (*mem_set)(void *, int, unsigned long);
    int (*mem_cpy)(void *, void *, unsigned long); int (*mem_read)(void *, void *, unsigned long);
    int (*mem_write)(void *, void *, unsigned long); int (*setup)(void); int (*shutdown)(void);
    void *(*palloc_secure)(int, void *, void *); unsigned int (*get_ve_addr_offset)(void);
};
typedef struct ScMemOpsS *(*get_ops_fn)(void);

int main(void)
{
    void *library = dlopen("libMemAdapter.so", RTLD_NOW | RTLD_LOCAL);
    get_ops_fn get_ops;
    struct ScMemOpsS *ops;
    int result;

    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    get_ops = (get_ops_fn)dlsym(library, "MemAdapterGetOpsS");
    if (!get_ops || !(ops = get_ops()) || !ops->open || !ops->close || !ops->palloc ||
        !ops->pfree || !ops->ve_get_phyaddr || !ops->ve_get_viraddr) {
        fprintf(stderr, "MemAdapter ABI is incomplete\n"); return 1;
    }
    result = ops->open();
    if (result) { fprintf(stderr, "MemAdapter open failed (legacy VE driver active?): %d\n", result); return 2; }
    if (ops->total_size() != 0 || ops->get_ve_addr_offset() != 0) {
        fprintf(stderr, "unexpected bridge defaults\n"); ops->close(); return 1;
    }
    ops->close();
    puts("MemAdapter ABI/open OK");
    return 0;
}
