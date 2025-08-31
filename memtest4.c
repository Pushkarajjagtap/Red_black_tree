
#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

	char buf[8192];
char name[3];
char *echoargv[] = {"echo", "ALL", "TESTS", "PASSED", 0};
int stdout = 1;
#define TOTAL_MEMORY (1 << 20) + (1 << 18)

void mem(void)
{
	// printf(1,"Test Started");
	int pid;
	uint prev_free_pages = getNumFreePages();
	long long size = ((prev_free_pages - 20) * 4096);

	printf(1, "Allocating %d bytes for each process\n", size);

	char *outer_malloc = (char *)malloc(100 * 4096);

	for (int i = 0; i < 100 * 4096; i++)
	{
		outer_malloc[i] = (char)(65 + i % 26);
	}

	pid = fork();
	// printf(1,"fork done\n");
	int x = 0;
hello:

	if (pid > 0)
	{
		if (x == 0)
		{
			char *parent_malloc = (char *)malloc(50 * 4096);
			for (int i = 0; i < 50 * 4096; i++)
			{
				parent_malloc[i] = (char)(65 + i % 26);
			}
			printf(1, "Parent alloc-ed\n");
		}

		wait();

		pid = fork();

		if (x < 500)
		{
			printf(1, "x:%d\n", x);
			x++;
			goto hello;
		}
	}

	else if (pid < 0)
	{
		printf(1, "Fork Failed\n");
	}

	else
	{
		sleep(100);

		char *malloc_child = (char *)malloc(size);

		for (int i = 0; i < size; i++)
		{
			malloc_child[i] = (char)(65 + i % 26);
		}

		printf(1, "Child alloc-ed\n");
	}

	if (pid > 0)
		printf(1, "Casual test case Passed !\n");
	exit();

	// failed:
	printf(1, "Casual test case Failed!\n");
	exit();
}

int main(int argc, char *argv[])
{
	// printf(1, "Memtest starting\n");
	mem();
	// getrss();
	exit();
	return 0;
}