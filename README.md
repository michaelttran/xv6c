xv6C

Authors Noah King, Michael Tran

Containers implemented in xv6 using a table of a new struct container that holds all information to
isolate a filesystem and process namespace as well as enforcing limits to memory disk space and processes.

The table mimics ptable and keeps an array of all containers.

	struct container {
		int cid;
		int total/used memory;
		int total/used disk;
		struct inode *root_dir;
		struct proc *inner_ptable[NPROC];
		enum procstate save_stat[NPROC];
		int vc_node;
		char name[16];
		uint ticks; 
		uint last_tick;
		int awake; 
		int toKill;
	}

Added a pointer to the struct container that is associated with a process in the established struct proc.

Virtual consoles are implemented to be attached to containers.  They can be cycled through using C^T.

Process Isolation:

	Root container is not respectiveresented as a container, but instead as the main console in xv6 and processes with no container are considered in the root.  

	Each container has an inner_ptable to enforce process isolation. To enforce isolation, any time a different process will be accessed, it will try to find the process in only the inner_ptable of the current processes container.
	
	The inner_ptable will assign available spots upon the allocation of another process in a fork() system call. There is a special case for the first program started in a container, for which there is a seperate system call cfork() which will assign the forked processes container forcibly based on a given cid.

File System Isolation:
	File isolation is done through namei by using the root_dir saved in the container struct in all instances where the root would be used in the function.  As well, when the path “..” is parsed while within the container’s root_dir, the path is interpreted as the container’s root instead of the real path along the filesystem.

Process Limits:
	Process limits are enforced from within allocproc() and fork() in proc.c.  If the allocproc() call is within a container and finds no available spot in the container’s inner_ptable, then it will print an error message and set the container to be killed (with the data member tokill) once  it returns to the fork() call.

Memory Limits:
	Memory limits are enforced from within kalloc() in kalloc.c and growproc() in proc.c.  If kalloc() would be called from within a container to increment the used memory above the container’s limit, then it will print an error message and set the container to be killed (with the data member tokill) once it returns to the growproc() call.

Disk Space Limits:
	Disk space limits are enforced within the writei() and dirlink() calls in fs.c.  If a call to writei() from within a container that would increment the used disk past the limit, then it will print an error message and set the container to be killed (with the data member tokill) once it returns to the dirlink() call.

Global variables for total/used memory/diskspace and last tick for the ‘root container’ as well.
	These variables are used for the user level tools ps free and df as well as for a fair round robin scheduling so that containers are scheduled fairly by container rather than by process.


Ctool:
	There is a user level tool called ctool that allows the root container to perform various operations on containers
		These operations are : create, start, pause, resume, stop, and info

	Create:
		The ctool function create simply spawns a file system to be used by a container later.  It makes a directory based on a given argument and copies a list of files into that directory.

	Start:
		The ctool function start will spawn a container with a given virtual console and directory as well as a program to start with optional flags for setting custom limits to the max processes, disk space, and memory allowed.  Start will execute the given program using a call to the special system call cfork() on the cid of the container that it spawned with the system call spawncont().  Cfork forks a new process into the container with the given cid.  It will then execute the given program in this container to be used later through the attached virtual console.

	Pause:
		The ctool function pause will simply put to sleep all processes in the container with a given cid, saving their states to be resumed to later, and then sleep the container so that it will not be scheduled.

	Resume:
		The ctool function resume will restore all processes in the container with a given cid to ther saved states, and then wake up the container so that it can be scheduled.

	Stop:
		The ctool function stop will kill a container with a given cid, resetting the struct to it's default values and killing all processes in the container.

	Info:
		The ctool function info will print out information about each container. It will print:
			Processes running in it
			The root directory
			The max processes, memory, and disk space
			The usable processes, memory, and disk space
			Execution statistics of the container and it's processes 
 
Ps:
	The user level tool ps prints information about the current container's processes (or all processes if within the root).

Df:
	The user level tool df prints information about the current container's disk space (or all disk space if within the root).

Free:
	The user level tool free prints information about the current container's memory (or all memory if within the root).
