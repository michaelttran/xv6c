#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

#include "fcntl.h"
#define BUFF_SIZE 512

// Helper function to copy files into the directory for create
void
copy(char* src, char *dst)
{
	int srcFD, destFD, nbread;
	char *buff[BUFF_SIZE];

	srcFD = open(src, O_RDONLY);
	if(srcFD == -1) {
		printf(1, "\nError opening file %s errno = %d\n", dst);
		exit();	
	}
	
	destFD = open(dst, O_CREATE | O_WRONLY);
	if(destFD == -1) {
		printf(1, "\nError opening file errno = %d\n");
	}

	while((nbread = read(srcFD,buff,BUFF_SIZE)) > 0)
	{
		if(write(destFD,buff,nbread) != nbread)
		printf(1, "\nError in writing data to %s\n", dst);
	}

	if(nbread == -1)
		printf(1, "\nError in reading data from %s\n", src);
	
	if(close(srcFD) == -1)
		printf(1, "\nError in closing file %s\n", src);
 
	if(close(destFD) == -1)
		printf(1, "\nError in closing file %s\n", dst);
}

// Traverses the filesystem upon start to get an initial used_disk
int
ls_help(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;
  int used_disk;

  if((fd = open(path, 0)) < 0){
    printf(2, "start: cannot open %s\n", path);
    return -1;
  }

  if(fstat(fd, &st) < 0){
    printf(2, "start: cannot stat %s\n", path);
    close(fd);
    return -1;
  }

  switch(st.type){
  case T_FILE:
    printf(1, "%s is not a directory! \n", path);
    return -1;
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "start: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf(1, "start: cannot stat %s\n", buf);
        continue;
      }
      used_disk += st.size;
    }
    break;
  }
  close(fd);
  return used_disk;
}

int main(int argc, char **argv) {

	if (argc < 2) {
		printf(1, "usage: ctool <tool> <args>\n");
		exit();
	}

	int cid = getcid();

	if (cid != 0) {
		printf(1, "cannot call ctool from within a container\n");
		exit();
	}

		/*
			Should throw errors if attempting to start programs on used
			virtual consoles in a container or if the container to start
			already has a virtual console attached to it.
 		*/
 	if (strcmp(argv[1], "start") == 0) {
 		if (argc < 5) {
 			printf(1, "usage: ctool start <vc#> <container directory> [-p <max_processes>] [-m <max_memory>] [-d <max_disk>] prog [arg1 arg2 ...]\n");
 			exit();
 		}
 		int proc = 0;
 		int mem = 0;
 		int disk = 0;
 		int prog = 4;
 		if (strcmp(argv[4], "-p") == 0) {
 			// There is a max number of processes given
 			proc = atoi(argv[5]);
 			if (strcmp(argv[6], "-m") == 0) {
 				// There is a max number of processes and memory given
  				mem = atoi(argv[7]);
 				if (strcmp(argv[8], "-d") == 0) {
 					// Only a max number of processes, memory, and disk space given
 					disk = atoi(argv[9]);
 					prog = 10;
 				} else {
 					// Only a max memory and proc given
 					prog = 8;
 				}
 			} else if (strcmp(argv[6], "-d") == 0) {
 				// Only a max number of processes and disk space given
 				disk = atoi(argv[7]);
 				prog = 8;
 			} else {
 				// Only a max number of processes was given
 				prog = 6;
 			}
 		} else if (strcmp(argv[4], "-m") == 0) {
 			// There is a max amount of memory given
 			mem = atoi(argv[5]);
 			if (strcmp(argv[6], "-d") == 0) {
 				// Only a max amount of memory and disk space given
 				disk = atoi(argv[7]);
 				prog = 8;
 			} else {
 				// Only a max amount of memory given
 				prog = 6;
 			}
 		} else if (strcmp(argv[4], "-d") == 0) {
 			// Only a max amount of disk space given
 			disk = atoi(argv[5]);
 			prog = 6;
 		}
 		// No flags given

 		// Checks if argv[3] is a valid directory and traverses it to find the starting used_disk of the container
 		int used_disk = ls_help(argv[3]);
 		if (used_disk < 0) {
 			exit();
 		}

 		//execute start
 		char *minor = (char*)malloc(4);
 		char b;
 		int j = 2;
 		while ((b = argv[2][j++]) != '\0') {
 			minor[j - 3] = argv[2][j - 1];
 		}
 		minor[j - 3] = '\0';
 		int id, fd, cid;
 		int minor_node = atoi(minor) + 2;

 		if ((cid = cstart(minor_node, argv[3], used_disk, proc, mem, disk)) < 0) {
 			printf(1, "start failed\n");
 			exit();
 		}

 		fd = open(argv[2], O_RDWR);

 		id = cfork(cid);

 		if(id == 0) {
 			chdir(argv[3]);
 			close(0);
		    close(1);
		    close(2);
		    dup(fd);
		    dup(fd);
		    dup(fd);
		    exec(argv[prog], &argv[prog]);
		    exit();
 		}

		exit();
	}

	if (strcmp(argv[1], "create") == 0) {

		int id;

		if (argc < 3) {
			printf(1, "usage: ctool create <container dir> [<file1> <file2> ...]\n");
			exit();
		}

		id = fork();
		
		if (id == 0) {
			char **args = (char**)malloc(64);
			args[0] = "mkdir";
			args[1] = argv[2];
			exec("mkdir", args);
			exit();
		}

		wait();

		// Copying all files into created directory
		int file_count = 2;

		while (++file_count < argc) {

			char *dst = (char*) malloc(32);

			int i, j;
			for (i = 0 ; i < strlen(argv[2]) ; i++) {
				dst[i] = argv[2][i];
			}
			dst[i++] = '/';

			for (j = 0 ; j < strlen(argv[file_count]) ; j++) {
				dst[i + j] = argv[file_count][j];
			}
			dst[i + j] = '\0';


			copy(argv[file_count], dst);
			

		}
		exit();
	}

	if (strcmp(argv[1], "info") == 0) {		
		if(argc > 2) {
			printf(1, "usage: ctool info\n");
		}

		cinfo();

		exit();
	}

	if (strcmp(argv[1], "pause") == 0) {

		if (argc < 3) {
			printf(1, "usage: ctool pause <container id>\n");
		}

		if (cpause(atoi(argv[2])) < 0) {
			printf(1, "No container with cid:%d\n", atoi(argv[2]));
		}

		exit();
	}

	if (strcmp(argv[1], "resume") == 0) {

		if (argc < 3) {
			printf(1, "usage: ctool resume <container id>\n");
		}

		if (cresume(atoi(argv[2])) < 0) {
			printf(1, "No container with cid:%d\n", atoi(argv[2]));
		}

		exit();
	}

	if (strcmp(argv[1], "stop") == 0) {

		if (argc < 3) {
			printf(1, "usage: ctool stop <container id>\n");
		}

		if (cstop(atoi(argv[2])) < 0) {
			printf(1, "No container with cid:%d\n", atoi(argv[2]));
		}

		exit();
	}

	exit();
}