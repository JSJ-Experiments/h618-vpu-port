// SPDX-License-Identifier: MIT
/* Optional private-run diagnostic: mirror Android vendor logs to stderr. */
#include <stdarg.h>
#include <stdio.h>

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
