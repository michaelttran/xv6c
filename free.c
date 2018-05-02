#include "types.h"
#include "user.h"
#include "fcntl.h"

int main(void) {
	// Calls a system call that accesses memory based on current process's cid
	
	writemem();

	exit();
}
