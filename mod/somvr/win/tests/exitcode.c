/* exitcode.c - win/wintests.py's canary, run first under wine: it must hand back the exit code 7 and this line, or no
 * test's verdict would be seen (every test would "pass") */
#include <stdio.h>

int main(void)
{
	printf("exitcode: 7\n");
	return 7;
}
