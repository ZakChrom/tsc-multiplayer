#ifndef TSC_UTILS
#define TSC_UTILS

#include <stddef.h>

const char *tsc_strintern(const char *str);
int tsc_streql(const char *a, const char *b);
char *tsc_strdup(const char *str);
unsigned long tsc_strhash(const char *str);

// Replaces the / with \ on Windows
void tsc_pathfix(char *path);
char tsc_pathsep();

char *tsc_allocfile(const char *path, size_t *len);
void tsc_freefile(char *memory);
int tsc_hasfile(const char *path);
// You must free path. The extension comes out of that memory.
// It replaces the frist . if any with a null terminator.
// NULL if there is no extension.
const char *tsc_fextension(char *path);

char **tsc_dirfiles(const char *path, size_t *len);
void tsc_freedirfiles(char **dirfiles);

#endif
