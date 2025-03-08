#ifndef UTILS_H
#define UTILS_H

#ifndef DISABLE_DEBUG
#define DEBUG_LOG(fmt, ...) \
do { fprintf(stdout, fmt, __VA_ARGS__); } while (0)
#else
#define DEBUG_LOG(fmt, ...) ((void)0)
#endif

#define UNUSED(var) ((void)(var))

#define ARRAY_SIZE(s) (sizeof(s) / sizeof((s)[0]))

#endif
