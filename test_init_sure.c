#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "sure.h"

#define SERVER "127.0.0.1"
#define APP_MSG_SIZE 1024
#define PORT 3001

int main() {
  int ret;
  sure_socket_t s;

  ret = sure_init(SERVER, PORT, SURE_SENDER, &s);
  printf(" the return value of init  is : %d \n", ret);

  sure_close(&s);  // end the connection
  return EXIT_SUCCESS;
}
