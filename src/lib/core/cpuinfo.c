//
// Created by weijing on 2019/11/6.
//

#include "cpuinfo.h"
#include "string.h"

sky_uint_t sky_cache_line_size;

#if ((__i386__ || __amd64__) && (__GNUC__ || __INTEL_COMPILER))

static sky_inline void cpu_id(sky_uint32_t i, sky_uint32_t* buf);

#if (__i386__)

static ngx_inline void
cpu_id(sky_uint32_t i, sky_uint32_t* buf)
{

    /*
     * we could not use %ebx as output parameter if gcc builds PIC,
     * and we could not save %ebx on stack, because %esp is used,
     * when the -fomit-frame-pointer optimization is specified.
     */

    __asm__ (

    "    mov    %%ebx, %%esi;  "

    "    cpuid;                "
    "    mov    %%eax, (%1);   "
    "    mov    %%ebx, 4(%1);  "
    "    mov    %%edx, 8(%1);  "
    "    mov    %%ecx, 12(%1); "

    "    mov    %%esi, %%ebx;  "

    : : "a" (i), "D" (buf) : "ecx", "edx", "esi", "memory" );
}


#else /* __amd64__ */


static sky_inline void
cpu_id(sky_uint32_t i, sky_uint32_t* buf) {
    sky_uint32_t eax, ebx, ecx, edx;

    __asm__ (

    "cpuid"

    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (i));

    buf[0] = eax;
    buf[1] = ebx;
    buf[2] = edx;
    buf[3] = ecx;
}


#endif


/* auto detect the L2 cache line size of modern and widespread CPUs */

void
sky_cpu_info() {
    sky_uchar_t* vendor;
    sky_uint32_t vbuf[5], cpu[4], model;

    vbuf[0] = 0;
    vbuf[1] = 0;
    vbuf[2] = 0;
    vbuf[3] = 0;
    vbuf[4] = 0;

    cpu_id(0, vbuf);

    vendor = (sky_uchar_t* ) &vbuf[1];

    if (vbuf[0] == 0) {
        return;
    }

    cpu_id(1, cpu);

    if (sky_strcmp(vendor, "GenuineIntel") == 0) {

        switch ((cpu[0] & 0xf00) >> 8) {

            /* Pentium */
            case 5:
                sky_cache_line_size = 32;
                break;

                /* Pentium Pro, II, III */
            case 6:
                sky_cache_line_size = 32;

                model = ((cpu[0] & 0xf0000) >> 8) | (cpu[0] & 0xf0);

                if (model >= 0xd0) {
                    /* Intel Core, Core 2, Atom */
                    sky_cache_line_size = 64;
                }

                break;

                /*
                 * Pentium 4, although its cache line size is 64 bytes,+
        −
                 * it prefetches up to two cache lines during memory read
                 */
            case 15:
                sky_cache_line_size = 128;
                break;
        }

    } else if (sky_strcmp(vendor, "AuthenticAMD") == 0) {
        sky_cache_line_size = 64;
    }
}

#else


void
sky_cpu_info()
{
    sky_cache_line_size = 32;
}


#endif