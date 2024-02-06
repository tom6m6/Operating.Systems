#include "kernel/system.h"
#include "kernel/vm.h"
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <minix/endpoint.h>
#include <minix/u64.h>

#if USE_CHRT

/*===========================================================================*
 *				do_chrt				     *
 *===========================================================================*/
int do_chrt(struct proc* caller, message* m_ptr) {
    struct proc* p;
    p=proc_addr(m_ptr->m2_i1);
    p->deadline=m_ptr->m2_l1;
    return OK;
}


#endif /* USE_CHRT */