/* MODIFY THIS FILE
 * together with sure.c, this implements the SURE protocol
 * for reliable data transfer.
 */
/* The comments in the functions are just hints on what to do,
 * but you are not forced to implement the functions as asked
 * (as long as you respect the interface described in sure.h
 * and that your protocol works)
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include "sure.h"


int sure_read(sure_socket_t *s, char *msg, int msg_size) {
    //wait if there isn't anything in the buffer (we'll be signaled by the other thread)
    //if we are not connected, return 0
    //take as many packets as there are in the buffer and append
    //them into the message that will be returned to the application (respecting the
    //msg_size limitation)
    //return the number of bytes written to msg
}

int sure_write(sure_socket_t *s, char *msg, int msg_size) {
    //break the application message into multiple SURE packets
    //add them to the buffer (wait if the buffer is full)
    //must do a memory copy because the application buffer may be reused right away
    //send the packets that fall within the window
}

int sure_init(char *receiver, int port, int side, sure_socket_t *p) {
    //fill the sure_socket_t
    //call udt_init
    //start thread (the receiver will need a thread that receives packets and add them to a buffer, the sender will need a thread that receives acks and removes packets from the buffer, or that retransmits if needed)
    //start connection (and wait until it is established)
}

void sure_close(sure_socket_t *s) {
    //end the connection (and wait until it is done)
    //call udt_close
    //call pthread_join for the thread
}



