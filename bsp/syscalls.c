/* Minimal newlib syscall stubs so printf() works over USART1.
 * Linked with --specs=nano.specs and -nostartfiles. */
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "bsp.h"

int _write(int fd, char *ptr, int len)
{
    (void)fd;
    bsp_uart_write(ptr, (unsigned)len);
    return len;
}

void _exit(int status)
{
    (void)status;
    for (;;) { }
}

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _lseek(int fd, int ptr, int dir)
{
    (void)fd; (void)ptr; (void)dir;
    return 0;
}

int _read(int fd, char *ptr, int len)
{
    (void)fd; (void)ptr; (void)len;
    return 0;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}

/* Tiny heap for newlib's malloc (printf in nano doesn't use it, but the
 * linker may still pull in _sbrk). The RTOS will get its own heap later. */
static unsigned char s_heap[2048];
static unsigned char *s_brk = s_heap;

caddr_t _sbrk(int incr)
{
    if ((s_brk + incr) > (s_heap + sizeof(s_heap))) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }
    unsigned char *prev = s_brk;
    s_brk += incr;
    return (caddr_t)prev;
}
