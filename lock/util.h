#ifndef UTIL_H_
#define UTIL_H_ 1

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define CLAMP(v,min,max) ((v) < (min)) ? (min) : ((v) > max) ? (max) : (v)

double util_gettime();

void util_strtolower(char* string);

#ifdef WIN32
void sincosf(float x, float *s, float *c);
int lrintf(float v);

struct dirent
{
  char *d_name;
};

typedef struct __DIR DIR;

DIR* opendir (const char *name);
void rewinddir (DIR* d);
int closedir (DIR* d);
struct dirent *readdir (DIR* d);
#endif

#endif /* !UTIL_H_ */
