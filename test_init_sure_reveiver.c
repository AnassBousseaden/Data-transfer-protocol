#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "sure.h"

#define APP_MSG_SIZE 1024
#define PORT 3001

int main() {
  int ret;
  sure_socket_t s;

  // create our "socket" using our SURE protocol (and establish the
  // connection)
  ret = sure_init(NULL, PORT, SURE_RECEIVER, &s);
  printf("%d\n", ret);
  if (ret != SURE_SUCCESS) exit(EXIT_FAILURE);

  return EXIT_SUCCESS;
}
