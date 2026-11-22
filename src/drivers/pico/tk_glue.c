/*
 * Pico T-Kernel glue. The syscall objects in src/kernel call Imalloc,
 * Icalloc, and Ifree. Those three go to B-RT. libstr.c is not linked.
 * The clean-room scheduler in core_pico.c still owns the shell.
 * Dispatch stays the portable side of the broadcom headers: this core
 * has no ARM SVC frame. pico_tk_retain keeps the syscall objects in
 * the image; startup still enters pico_kernel_entry.
 */
#include <stddef.h>
#include <stdint.h>

void rt_alloc_init(void *pool, size_t bytes);
void *rt_malloc(size_t n);
void rt_free(void *ptr);
void *memset(void *dst, int c, size_t n);

static unsigned char s_pool[2048] __attribute__((aligned(8)));
static int s_ready;

void *Imalloc(size_t n)
{
    if (!s_ready) {
        rt_alloc_init(s_pool, sizeof s_pool);
        s_ready = 1;
    }
    return rt_malloc(n);
}

void *Icalloc(size_t nmemb, size_t size)
{
    size_t n;
    void *p;

    if (size != 0 && nmemb > ((size_t)-1) / size)
        return 0;
    n = nmemb * size;
    p = Imalloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

void Ifree(void *ptr)
{
    rt_free(ptr);
}

void *IAmalloc(size_t n, unsigned int attr)
{
    (void)attr;
    return Imalloc(n);
}

void IAfree(void *ptr, unsigned int attr)
{
    (void)attr;
    Ifree(ptr);
}

/* Little-endian bit string: offset 0 is the LSB of base[0]. */
void BitSet(void *base, unsigned long offset)
{
    unsigned char *p = (unsigned char *)base;
    p[offset / 8] |= (unsigned char)(1u << (offset % 8));
}

unsigned long available_cop;

unsigned int disint(void) { return 0; }
unsigned int enaint(unsigned int intsts) { return intsts; }
void DisableInt(unsigned int vec) { (void)vec; }
void EnableInt(unsigned int vec) { (void)vec; }
void SetIntMode(unsigned int vec, unsigned int mode) { (void)vec; (void)mode; }
void ClearInt(unsigned int vec) { (void)vec; }

void hook_dsp(void) {}
void unhook_dsp(void) {}
void hook_int(void) {}
void unhook_int(void) {}
void hook_svc(void) {}
void unhook_svc(void) {}

int cpu_initialize(void) { return 0; }
void cpu_shutdown(void) {}
int tkdev_initialize(void) { return 0; }
void tkdev_exit(void) {}

void dispatch_to_schedtsk(void) {}
void dispatch_entry(void) {}
void call_entry(void) {}
void rettex_entry(void) {}
void _tk_ret_int(void) {}
void call_dbgspt(void) {}
void defaulthdr_startup(void) {}
void exchdr_startup(void) {}
void inthdr_startup(void) {}
void timer_handler_startup(void) {}

int no_support(void) { return -70; }
void request_tex(void *tcb) { (void)tcb; }
void low_pow(void) {}
void off_pow(void) {}
void tm_monitor(void) {}
void tm_putstring(const unsigned char *s) { (void)s; }
void tm_command(const char *cmd) { (void)cmd; }
void tm_exit(int code) { (void)code; }
void DispProgress(int n) { (void)n; }

int _tk_get_cfn(unsigned char *name, int *val, int max)
{
    (void)name;
    if (val && max > 0)
        val[0] = 0;
    return 1;
}

int __tk_get_cfn(unsigned char *name, int *val, int max)
{
    return _tk_get_cfn(name, val, max);
}

int GetDevConf(const unsigned char *name, int *val)
{
    (void)name;
    if (val)
        val[0] = 0;
    return 0;
}

int GetSysConf(const unsigned char *name, int *val)
{
    (void)name;
    if (val)
        val[0] = 0;
    return 0;
}

extern void t_kernel_main(void *inittask);
extern const char kernel_version[];

void pico_tk_retain(void)
{
    t_kernel_main(0);
    (void)kernel_version;
}
