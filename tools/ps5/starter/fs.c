/* The one function of websrv's src/fs.c that hbldr.c uses, without its web server.

   SPDX-License-Identifier: GPL-3.0-or-later */

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>

#include "fs.h"

uint8_t*
fs_readfile(const char* path, size_t* size) {
  struct stat st;
  uint8_t* buf;
  size_t done = 0;
  int fd;

  if((fd=open(path, O_RDONLY)) < 0) {
    return 0;
  }
  if(fstat(fd, &st) != 0 || !(buf=malloc(st.st_size ? st.st_size : 1))) {
    close(fd);
    return 0;
  }
  while(done < (size_t)st.st_size) {
    ssize_t n = read(fd, buf + done, st.st_size - done);
    if(n <= 0) {
      free(buf);
      close(fd);
      return 0;
    }
    done += n;
  }
  close(fd);
  if(size) {
    *size = done;
  }
  return buf;
}
