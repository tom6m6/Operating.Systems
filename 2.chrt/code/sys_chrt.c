#include "syslib.h"

int sys_chrt(endpoint_t proc_ep, long deadline){
    message newm;
    newm.m2_i1=proc_ep;
    newm.m2_l1=deadline;
    return _kernel_call(SYS_CHRT,&newm);
}