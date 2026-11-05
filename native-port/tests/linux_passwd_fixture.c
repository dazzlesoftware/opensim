// Child-process-only NSS fixture. Never changes host accounts or identity.
#define _GNU_SOURCE
#include <pwd.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
uid_t geteuid(void) {
    const char *mode = getenv("OPENSIM_TEST_PASSWD");
    if (mode && strcmp(mode, "effective") == 0)
        return 12345;
    uid_t (*next)(void) = dlsym(RTLD_NEXT, "geteuid");
    return next();
}
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t length, struct passwd **result) {
    const char *mode = getenv("OPENSIM_TEST_PASSWD");
    if (!mode) {
        int (*next)(uid_t, struct passwd *, char *, size_t, struct passwd **) =
            dlsym(RTLD_NEXT, "getpwuid_r");
        return next(uid, pwd, buffer, length, result);
    }
    const char *trace = getenv("OPENSIM_TEST_PWTRACE");
    if (trace) {
        FILE *file = fopen(trace, "a");
        if (file) {
            fprintf(file, "%u %zu\n", (unsigned)uid, length);
            fclose(file);
        }
    }
    *result = NULL;
    if (strcmp(mode, "missing") == 0)
        return 0;
    if (strcmp(mode, "error") == 0)
        return EIO;
    static atomic_uint calls = 0;
    if (strcmp(mode, "eintr") == 0 && atomic_fetch_add(&calls, 1) == 0)
        return EINTR;
    memset(pwd, 0, sizeof(*pwd));
    if (strcmp(mode, "null") == 0) {
        *result = pwd;
        return 0;
    }
    const char *home = "/synthetic/passwd-home";
    char large[1026];
    if (strcmp(mode, "long") == 0) {
        memset(large, 'a', sizeof(large));
        large[0] = '/';
        large[1025] = 0;
        home = large;
    }
    if (strcmp(mode, "empty") == 0)
        home = "";
    if (strcmp(mode, "invalidutf8") == 0)
        home = "/synthetic/\xff";
    if (strcmp(mode, "effective") == 0) {
        if (uid != 12345)
            return EIO;
        home = "/synthetic/effective-12345";
    }
    const size_t count = strlen(home) + 1;
    if (length < count)
        return ERANGE;
    memcpy(buffer, home, count);
    pwd->pw_dir = buffer;
    pwd->pw_uid = uid;
    *result = pwd;
    return 0;
}
