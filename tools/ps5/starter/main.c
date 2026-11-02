/* Pokemon Fulldex's starter: a payload for the console's ELF loader (elfldr, port 9021) that
   starts the program, eboot.elf, as websrv's homebrew list does, without websrv.

   The app on the Home screen (tools/ps5/launcher, registered by ShadowMountPlus) sends this to
   the loader when it is started, and the program sends it when QUIT goes back to the game list.
   It closes whatever application is running (the app, or the program), starts websrv's
   placeholder application FAKE00000 and puts eboot.elf in its place (hbldr_launch, from
   ps5-payload-websrv's src/ps5/hbldr.c, fetched by tools/ps5/build.sh). That way the program
   runs exactly as it does from websrv's list: as an application, with the rights a payload has
   (its JIT, /data, the graphics chip).

   SPDX-License-Identifier: GPL-3.0-or-later */

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include <ps5/kernel.h>

#include "hbldr.h"

#define TITLE_ID "PKFD00001"

int sceUserServiceInitialize(void*);
int sceKernelSendNotificationRequest(int, void*, size_t, int);

static void
notify(const char* text) {
  struct {
    char unused[45];
    char message[3075];
  } req;

  memset(&req, 0, sizeof req);
  snprintf(req.message, sizeof req.message, "Pokemon Fulldex: %s", text);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

/* Whether dir holds the program: eboot.elf beside an sce_sys/param.json with this title, or
   the folder websrv's homebrew list used before (no param.json). */
static int
holds_program(const char* dir, int needTitle) {
  char path[512];
  struct stat st;

  snprintf(path, sizeof path, "%s/eboot.elf", dir);
  if(stat(path, &st) != 0) {
    return 0;
  }
  if(!needTitle) {
    return 1;
  }
  snprintf(path, sizeof path, "%s/sce_sys/param.json", dir);
  FILE* f = fopen(path, "r");
  if(!f) {
    return 0;
  }
  char buf[4096];
  size_t n = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[n] = 0;
  return strstr(buf, "\"" TITLE_ID "\"") != 0;
}

/* The program's folder: the app's own folder in a scan path of ShadowMountPlus's (its name is
   usually the title, but any folder with its param.json will do), else websrv's old one. */
static int
find_program(char* out, size_t size) {
  static const char* bases[] = {
    "/data/homebrew", "/mnt/usb0/homebrew", "/mnt/usb1/homebrew", "/mnt/usb2/homebrew",
    "/mnt/usb3/homebrew", "/mnt/usb4/homebrew", "/mnt/usb5/homebrew", "/mnt/usb6/homebrew",
    "/mnt/usb7/homebrew", "/mnt/ext0/homebrew", "/mnt/ext1/homebrew",
  };

  for(size_t i=0; i<sizeof bases / sizeof bases[0]; i++) {
    snprintf(out, size, "%s/" TITLE_ID, bases[i]);
    if(holds_program(out, 1)) {
      return 0;
    }
    DIR* d = opendir(bases[i]);
    if(!d) {
      continue;
    }
    struct dirent* e;
    while((e=readdir(d))) {
      if(e->d_name[0] == '.') {
        continue;
      }
      snprintf(out, size, "%s/%s", bases[i], e->d_name);
      if(holds_program(out, 1)) {
        closedir(d);
        return 0;
      }
    }
    closedir(d);
  }
  snprintf(out, size, "/data/homebrew/PokemonFulldex");
  return holds_program(out, 0) ? 0 : -1;
}

int
main(void) {
  char dir[512];
  char path[600];
  char* argv[] = {0};
  char* envp[] = {0};

  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);
  /* (As websrv: the user service for the foreground user, and the identity that may close and
     start applications.) */
  if(sceUserServiceInitialize(0)) {
    perror("sceUserServiceInitialize");
  }
  kernel_set_ucred_authid(-1, 0x4801000000000013L);

  if(find_program(dir, sizeof dir)) {
    notify("eboot.elf not found (in /data/homebrew/" TITLE_ID ")");
    return 1;
  }
  snprintf(path, sizeof path, "%s/eboot.elf", dir);
  printf("starting %s\n", path);
  /* (No standard output for the program: its log is game.log, which it opens itself.) */
  if(hbldr_launch(dir, path, -1, argv, envp) < 0) {
    notify("could not start eboot.elf");
    return 1;
  }
  return 0;
}
