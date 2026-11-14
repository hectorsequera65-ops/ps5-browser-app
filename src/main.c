#include <stdio.h>
#include <unistd.h>
#include "browser.h"
#include "eden_bridge.h"

extern int sceNetCtlInit();
extern int sceUserServiceInitialize(void *);

int main(int argc, char *argv[]) {
  log_msg("[EDEN BRIDGE] Starting LAN bridge\n");

  sceNetCtlInit();

  int prio = 256;
  sceUserServiceInitialize(&prio);

  notify("Eden Bridge running (keep payload active)");
  return eden_bridge_run();


}
