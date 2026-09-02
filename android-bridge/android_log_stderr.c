// SPDX-License-Identifier: MIT
/* Optional private-run diagnostic: mirror Android vendor logs to stderr. */
#include <stdarg.h>
#include <stdio.h>

/* CedarX's debug branches emit register and SRAM values when this is <= 3.
 * Keep the diagnostic preload self-contained instead of patching libcdc_base. */
__attribute__((visibility("default"))) int GLOBAL_LOG_LEVEL = 2;

__attribute__((visibility("default"))) int __android_log_print(int priority,
                                                                 const char *tag,
                                                                 const char *format, ...)
{
    va_list args;
    (void)priority;
    fprintf(stderr, "[%s] ", tag ? tag : "android");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    return 0;
}
