#ifndef _SYS_SMP_H
#define _SYS_SMP_H

#define MAX_CPUS 32

void smp_init(void);
int smp_get_cpu_count(void);
void smp_discover_cores(void);

#endif
