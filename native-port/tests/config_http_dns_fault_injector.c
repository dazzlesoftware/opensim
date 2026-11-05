#define _GNU_SOURCE
#include <netdb.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                struct addrinfo** result) {
    struct Entry {
        const char* host;
        int error;
    };
    static const struct Entry entries[] = {{"noname.opensim-parity.invalid", EAI_NONAME},
                                           {"again.opensim-parity.invalid", EAI_AGAIN},
                                           {"fail.opensim-parity.invalid", EAI_FAIL},
                                           {"nodata.opensim-parity.invalid", EAI_NODATA},
                                           {"addrfamily.opensim-parity.invalid", EAI_ADDRFAMILY},
                                           {"family.opensim-parity.invalid", EAI_FAMILY},
                                           {"badflags.opensim-parity.invalid", EAI_BADFLAGS},
                                           {"service.opensim-parity.invalid", EAI_SERVICE},
                                           {"memory.opensim-parity.invalid", EAI_MEMORY},
                                           {"system.opensim-parity.invalid", EAI_SYSTEM}};
    for (unsigned i = 0; node && i < sizeof(entries) / sizeof(entries[0]); ++i)
        if (!strcmp(node, entries[i].host)) {
            *result = 0;
            fprintf(stderr, "RESOLVER:%s:%d\n", node, entries[i].error);
            if (entries[i].error == EAI_SYSTEM)
                errno = EACCES;
            return entries[i].error;
        }
    typedef int (*Lookup)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
    Lookup next = (Lookup)dlsym(RTLD_NEXT, "getaddrinfo");
    if (!next) {
        errno = ENOSYS;
        return EAI_SYSTEM;
    }
    return next(node, service, hints, result);
}
