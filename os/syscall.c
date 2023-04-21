#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"
#include "riscv.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	TimeVal k_val;
	// YOUR CODE
	struct proc *p = curr_proc();

	/* The code in `ch3` will leads to memory bugs*/

	uint64 cycle = get_cycle();
	k_val.sec = cycle / CPU_FREQ;
	k_val.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable,(uint64)val,(char *)&k_val,sizeof(TimeVal));
	return 0;
}

uint64 sys_sbrk(int n)
{
	uint64 addr;
        struct proc *p = curr_proc();
        addr = p->program_brk;
        if(growproc(n) < 0)
                return -1;
        return addr;	
}



// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/

// this syscall handles va unmap and pa free when map failed
uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd){

	// fall early

	if((port & ~0x7) != 0){
		errorf("mmap:port contain dirty bits");
		return 0-1;
	}
	if((port & 0x7) == 0 ){
		errorf("mmap:request non R or W or X memory is meaningless");
		return -1;
	}
	if(!PGALIGNED(start)){
		errorf("mmap:start address is not page aligned");
		return -1;
	}
	if(len == 0){
		return 0;
	}
	if(len > 1024 * 1024 * 1024){
		errorf("mmap:too large");
		return -1;
	}

	// do the job

	int perm = PTE_U;
	if(port & 0b1u ){
		perm |= PTE_R;
	}
	if(port & 0b10u ){
		perm |= PTE_W;
	}
	if(port & 0b100u ){
		perm |= PTE_X;
	}
	perm &= (0b11110u);

    struct proc *p = curr_proc();

	int num = PGROUNDUP(len) / PAGE_SIZE;
	int mapped_pages = 0;

	uint64 pa = 0;
	uint64 va = start;

	while(mapped_pages < num){
		pa = (uint64) kalloc();
		if(pa == 0){
			// no p memory,unmap and free mapped page ,then fail;
			uvmunmap(p->pagetable,start,mapped_pages,1);
			errorf("mmap:no enough physical mem");
			return -1;
		}
		// map phical memory page to user virtual memory space
		if(mappages(p->pagetable, va, PAGE_SIZE, pa, perm) == -1){
		    //unmap,free and fail;
		    uvmunmap(p->pagetable,start,mapped_pages,1);

			// this pa is successfully alloced but not mapped,thus not freed by uvmunmap. 
			kfree((void *)pa);
			errorf("mmap:map va to pa failed,possibly already mapped");
			return -1;
	    }
		mapped_pages++;
		va += PAGE_SIZE;
	}

	return 0;
}

// this syscall "handles" va remap and pa realloc when unmap failed
uint64 sys_munmap(uint64 start, uint64 len){

	// fall early
	if(!PGALIGNED(start)){
		errorf("munmap:start address is not page aligned");
		return -1;
	}
	if(len == 0){
		return 0;
	}
	if(!PGALIGNED(len)){
		// not specified, choose to up align len
	}

	struct proc *p = curr_proc();

	int num = PGROUNDUP(len) / PAGE_SIZE;
	uint64 va = start;
	
	while(num > 0){
		// useraddr return 0 when no map, but what if the pa been mapped is 0...
		if(useraddr(p->pagetable,va) == 0){
		    errorf("munmap:va range contains page that not mapped to pm");
			return -1;
	    }
		va += PAGE_SIZE;
		num--;
	}
	// unmap and free at end, so as to "handle" va unmap and pa free when map failed
	uvmunmap(p->pagetable,start,PGROUNDUP(len) / PAGE_SIZE,1);
	return 0;
}

uint64 sys_task_info(TaskInfo* info){
	TaskInfo k_info;
	struct proc *p = curr_proc();
	uint64 current_time_ms = get_cycle() / CPU_FREQ * 1000 + (get_cycle() % CPU_FREQ) * 1000 / CPU_FREQ;

	k_info.status = Running;
	k_info.time = (int)(current_time_ms - p->time);
	memmove(k_info.syscall_times,p->syscall_times,sizeof(info->syscall_times));
	copyout(p->pagetable,(uint64)info,(char *)&k_info,sizeof(TaskInfo));
	return 0;
}


extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
    curr_proc()->syscall_times[id]++;

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
    case SYS_task_info:
	    ret = sys_task_info((TaskInfo *)args[0]);
		break;

	case SYS_mmap:
	    ret = sys_mmap((uint64)args[0],(uint64)args[1],args[2],args[3],args[4]);
		break;
	case SYS_munmap:
	    ret = sys_munmap((uint64)args[0],(uint64)args[1]);
		break;

	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
