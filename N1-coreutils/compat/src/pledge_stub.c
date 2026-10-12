#include <pledge.h>
#include <unveil.h>

int
pledge(const char *promises, const char *execpromises)
{
	(void)promises;
	(void)execpromises;
	return 0;
}

int
unveil(const char *path, const char *permissions)
{
	(void)path;
	(void)permissions;
	return 0;
}
