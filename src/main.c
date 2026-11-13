#include <stdio.h>
#include <unistd.h>
#include "browser.h"
#include "app_installer.h"
#include "eden_bridge.h"

extern int sceNetCtlInit();
extern int sceUserServiceInitialize(void *);

int main(int argc, char *argv[]) {
  log_msg("[EDEN ROOMS] Starting Eden Rooms PS5 shortcut installer\n");

  sceNetCtlInit();

  int prio = 256;
  sceUserServiceInitialize(&prio);

  if (install_app_if_needed() != 0) {
    log_msg("[EDEN ROOMS] Shortcut install failed; bridge not started.\n");
    return 1;
  }
  notify("Eden Bridge running (keep payload active)");
  return eden_bridge_run();


}
