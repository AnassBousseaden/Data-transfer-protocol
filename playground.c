#include <stdio.h>

int main(void) {
  unsigned char SYN = 0b1;
  unsigned char ACK = 0b01;
  unsigned char FIN = 0b001;

  printf("SYN : %c \n ACK : %c \n FIN : %c \n", SYN, ACK, FIN);

  return 0;
}