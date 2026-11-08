#include <unistd.h>
#include <stdio.h>
static inline int _isatty(int fd) { return isatty(fd); }
static inline int _fileno(FILE *fp) { return fileno(fp); }
