#include "syslib.h"

int sys_chrt(endpoint_t who,long deadline){
	message m;
	m.m2_i1 = who;
	m.m2_l1 = deadline;
	return _kernel_call(SYS_CHRT, &m);
}
