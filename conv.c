#include "stb_image.h"

void help()
{
	fputs("=w=\n", stderr);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		help();
		return 1;
	}
	return 0;
}
