#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/file.h>
static int fired;
static int ownpath(const char* p) {
    const char* r = getenv("OWN_ROOT");
    return r && p && !strncmp(p, r, strlen(r));
}
static int ownfd(int fd) {
    char key[64], value[4096];
    snprintf(key, sizeof key, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(key, value, sizeof(value) - 1);
    if (n < 0)
        return 0;
    value[n] = 0;
    return ownpath(value);
}
static int hit(const char* op, int own) {
    const char* t = getenv("INJECT_OP");
    if (own && t && !strcmp(t, op)) {
        char trace[120];
        int size = snprintf(trace, sizeof trace, "CALL %s\n", op);
        write(2, trace, size);
    }
    if (!fired && own && t && !strcmp(t, op)) {
        fired = 1;
        char b[120];
        int n = snprintf(b, sizeof b, "INJECTED %s\n", op);
        write(2, b, n);
        errno = EINTR;
        return 1;
    }
    return 0;
}
#define PATH_STAT(fn, ty)                                                                                    \
    int fn(const char* p, ty* s) {                                                                           \
        static int (*real)(const char*, ty*);                                                                \
        if (!real)                                                                                           \
            real = dlsym(RTLD_NEXT, #fn);                                                                    \
        if (hit("stat", ownpath(p)))                                                                         \
            return -1;                                                                                       \
        return real(p, s);                                                                                   \
    }
#define FD_STAT(fn, ty)                                                                                      \
    int fn(int fd, ty* s) {                                                                                  \
        static int (*real)(int, ty*);                                                                        \
        if (!real)                                                                                           \
            real = dlsym(RTLD_NEXT, #fn);                                                                    \
        if (hit("fstat", ownfd(fd)))                                                                         \
            return -1;                                                                                       \
        return real(fd, s);                                                                                  \
    }
PATH_STAT(stat, struct stat)
PATH_STAT(stat64, struct stat64)
FD_STAT(fstat, struct stat)
FD_STAT(fstat64, struct stat64)
DIR* opendir(const char* p) {
    static DIR* (*real)(const char*);
    if (!real)
        real = dlsym(RTLD_NEXT, "opendir");
    if (hit("opendir", ownpath(p)))
        return 0;
    return real(p);
}
int flock(int fd, int op) {
    static int (*real)(int, int);
    if (!real)
        real = dlsym(RTLD_NEXT, "flock");
    if (op != LOCK_UN && hit("flock", ownfd(fd)))
        return -1;
    return real(fd, op);
}
int ftruncate(int fd, off_t len) {
    static int (*real)(int, off_t);
    if (!real)
        real = dlsym(RTLD_NEXT, "ftruncate");
    if (hit("ftruncate", ownfd(fd)))
        return -1;
    return real(fd, len);
}
int ftruncate64(int fd, off64_t len) {
    static int (*real)(int, off64_t);
    if (!real)
        real = dlsym(RTLD_NEXT, "ftruncate64");
    if (hit("ftruncate", ownfd(fd)))
        return -1;
    return real(fd, len);
}

int __fxstat64(int ver, int fd, struct stat64* s) {
    static int (*real)(int, int, struct stat64*);
    if (!real)
        real = dlsym(RTLD_NEXT, "__fxstat64");
    if (hit("fstat", ownfd(fd)))
        return -1;
    return real(ver, fd, s);
}
int __xstat64(int ver, const char* p, struct stat64* s) {
    static int (*real)(int, const char*, struct stat64*);
    if (!real)
        real = dlsym(RTLD_NEXT, "__xstat64");
    if (hit("stat", ownpath(p)))
        return -1;
    return real(ver, p, s);
}

off_t lseek(int fd, off_t off, int whence) {
    static off_t (*real)(int, off_t, int);
    if (!real)
        real = dlsym(RTLD_NEXT, "lseek");
    if (hit("lseek", ownfd(fd)))
        return -1;
    return real(fd, off, whence);
}
off64_t lseek64(int fd, off64_t off, int whence) {
    static off64_t (*real)(int, off64_t, int);
    if (!real)
        real = dlsym(RTLD_NEXT, "lseek64");
    if (hit("lseek", ownfd(fd)))
        return -1;
    return real(fd, off, whence);
}
