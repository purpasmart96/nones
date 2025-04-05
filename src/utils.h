#ifndef UTILS_H
#define UTILS_H

#ifndef DISABLE_DEBUG
#define DEBUG_LOG(fmt, ...) \
do { fprintf(stdout, fmt, __VA_ARGS__); } while (0)
#else
#define DEBUG_LOG(fmt, ...) ((void)0)
#endif

#ifndef DISABLE_CPU_LOG
#define CPU_LOG(fmt, ...) \
do { fprintf(stdout, fmt, __VA_ARGS__); } while (0)
#else
#define CPU_LOG(fmt, ...) ((void)0)
#endif

#define UNUSED(var) ((void)(var))

#define ARRAY_SIZE(s) (sizeof(s) / sizeof((s)[0]))

#define GET_HIGH_LE(v) (((v >> 8) & 0xFF))
#define GET_LOW_LE(v) ((v & 0xFF))

#define GET_HIGH_BE(v) ((v & 0xFF))
#define GET_LOW_BE(v) (((v >> 8) & 0xFF))

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

#endif
