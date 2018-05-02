#include "types.h"
#include "stat.h"
#include "user.h"


/*
	Lists processes
	- In a container, it should only show processes running in that container
	- In the root container, it should show all processes and identify the container for each process
*/

// n = getps((char*)psarray)
int main(void) {

  writeprocs();
  exit();
}
 