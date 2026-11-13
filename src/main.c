#include <stdio.h>
#include <unistd.h>
#include "browser.h"
#include "app_installer.h"

extern int sceNetCtlInit();
extern int sceUserServiceInitialize(void *);

int main(int argc, char *argv[]) {
  log_msg("[EDEN ROOMS] Starting Eden Rooms PS5 shortcut installer\n");

  sceNetCtlInit();

  int prio = 256;
  sceUserServiceInitialize(&prio);

  install_app_if_needed();

  log_msg("[EDEN ROOMS] Done\n");
  return 0;
}
