/* rbp runs as `ld-linux.so.3 /root/pdj/rbp`, so /proc/self/exe names the loader.
 * The xdj-rx-tools mod (libxdjmod.so) reads that link at start-up and does
 * nothing, silently, unless the file name is "rbp". With XDJMOD=1 this answers
 * for that one path with the player's own, and forwards everything else. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include "shimutil.h"

#define RBP_EXE "/root/pdj/rbp"

ssize_t readlink(const char *path, char *buf, size_t size)
{
     static ssize_t (*real)(const char *, char *, size_t);

     if (!real)
          real = (ssize_t (*)(const char *, char *, size_t))dlsym(RTLD_NEXT, "readlink");
     if (!strcmp(path, "/proc/self/exe") && env_on("XDJMOD", 0)) {
          size_t n = sizeof RBP_EXE - 1;

          if (n > size)
               n = size;
          memcpy(buf, RBP_EXE, n);
          return (ssize_t)n;
     }
     return real ? real(path, buf, size) : -1;
}
