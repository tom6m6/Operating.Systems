#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <unistd.h>
#include <sys/time.h>
int chrt(long deadline) {
    message m;
    struct timespec now;
    memset(&m, 0, sizeof(m));
    if (deadline<=0)return 0;

    alarm((unsigned int)deadline);
    
    clock_gettime(CLOCK_REALTIME, &now);
	deadline+=now.tv_sec;
    
    m.m2_l1=deadline;
    return _syscall(PM_PROC_NR, PM_CHRT, &m);
}