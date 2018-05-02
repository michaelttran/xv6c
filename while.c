#include "types.h"
#include "stat.h"
#include "user.h"


int main (int argc, char **argv) {
	int x = 1;
	int id = fork();

	if (id == 0) {
		while (x) {
			x = 2;
		}
	}
	
	exit();
}
