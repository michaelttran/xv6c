// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "fs.h"

char *argv[] = { "sh", 0 };

void
create_vcs(void)
{
  int i, fd;
  char *dname = "vc0";

  for (i = 0; i < 8; i++) {
    dname[2] = '0' + i;
    if ((fd = open(dname, O_RDWR)) < 0){
      mknod(dname, 1, i + 2);
    } else {
      close(fd);
    }
  }
}

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

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  create_vcs();

  int used_disk = ls_help(".");
  tdiskused(used_disk);
  
  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }

}
