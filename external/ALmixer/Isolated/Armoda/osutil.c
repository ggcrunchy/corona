#include <fcntl.h>
#include <stdlib.h>
// STEVE CHANGE
#ifndef _WIN32
    #include <unistd.h>
#else
    int strcasecmp( const char* a, const char* b )
    {
        while (*a && *b)
        {
            int cmp = tolower(*a) - tolower(*b);

            if (tolower(*a++) != tolower(*b++)) return 1; // only need non-0
        }

        return !(*a == 0 && *b == 0); // ditto
    }
#endif
// /STEVE CHANGE
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

void fatal(const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    fprintf(stderr, "Fatal error:\n");
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\nExiting.\n");
    va_end(va);
    exit(1);
}

void *mallocsafe(size_t size)
{
    void *foo;

    if (size == 0) size = 1;
    foo = malloc(size);
    if (foo == NULL)
	fatal("Unable to allocate %i bytes of memory.\n", size);

    return foo;
}

void *strdupsafe(char *str)
{
    char *foo;
    int size;

    size = strlen(str);
    foo = (char *)mallocsafe(size+1);
    memcpy(foo, str, size+1);

    return foo;
}

