#ifndef ERROR_H_
#define ERROH_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NDEBUG
void info(const char* format, ...);
#else
#define info (void)
#endif

#ifdef WIN32
#define __attribute__(x)
#endif

void fatal_error(const char* format, ...) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* !ERROR_H_ */
