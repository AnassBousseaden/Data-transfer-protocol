/* MODIFY THIS FILE
 * together with sure.c, this implements the SURE protocol
 * for reliable data transfer.
 */
/* The comments in the functions are just hints on what to do,
 * but you are not forced to implement the functions as asked
 * (as long as you respect the interface described in sure.h
 * and that your protocol works)
 */
#include "sure.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SYN 0b1
#define ACK 0b10
#define FIN 0b100

int sure_read(sure_socket_t *s, char *msg, int msg_size) {
  // wait if there isn't anything in the buffer (we'll be signaled by the other
  // thread) if we are not connected, return 0 take as many packets as there are
  // in the buffer and append them into the message that will be returned to the
  // application (respecting the msg_size limitation) return the number of bytes
  // written to msg
}

int sure_write(sure_socket_t *s, char *msg, int msg_size) {
  // break the application message into multiple SURE packets
  // add them to the buffer (wait if the buffer is full)
  // must do a memory copy because the application buffer may be reused right
  // away send the packets that fall within the window
}
// thread that receive packets and add them to the buffer
void *receiver_thread(void *param) { return NULL; }
// thread that  receives acks and removes packets from the buffer, or that
// retransmits if needed
void *sender_thread(sure_socket_t *p) {
  // sure_packet_t buf_packet;
  int recv_status;
  int send_status;
  sure_packet_t last_ack;
  unsigned char flags;
  sure_packet_t packet;
  unsigned long time_in_nano;
  unsigned long time_in_micro;
  unsigned long time_t1;
  struct timespec tv;

  while (true) {
    if (p->num == 0) {
      continue;
    }
    // retransmitions
    tv = p->timers[p->index_timer];
    time_in_micro = TIME_IN_MICRO(tv);
    clock_gettime(CLOCK_MONOTONIC, &tv);
    time_t1 = TIME_IN_MICRO(tv);

    // check if there is a timeout
    if (time_t1 - time_in_micro >= SURE_TIMEOUT) {
      // start retransmitting
      udt_send(&p->udt, (char *)&p->buffer[p->start_window],
               sizeof(p->buffer[p->start_window]));

      continue;
    }
    // if no packet timed out , we can in the  spare time check for acks and
    // move the window
    udt_set_timeout(&p->udt, time_t1 - time_in_micro);
    recv_status = udt_recv(&p->udt, (char *)&last_ack, sizeof(last_ack));
    if (recv_status > 0) {
      // lock the ressource
      // CHECK FOR flags (FIN , SYN , ACK)
      if (last_ack.flags & ACK == ACK) {
        unsigned int ack_number = last_ack.seq_ack_number;
        while (true) {
          packet = p->buffer[p->start_window];
          if (packet.seq_ack_number >= ack_number) {
            break;
          }
          p->start_window = (p->start_window + 1) % SURE_BUFFER;
          p->num--;
          p->index_timer = (p->index_timer + 1) % SURE_WINDOW;
          // try to send the not yet sent packet at the edge of the window
          if (p->num >= SURE_WINDOW) {
            send_status = udt_send(&p->udt, (char *)&p->buffer[p->add],
                                   sizeof(p->buffer[p->add]));
            clock_gettime(CLOCK_MONOTONIC, &p->timers[SURE_WINDOW]);
            p->add++;
          }
        }
      }
    }
  }
  return;
}

int sure_init(char *receiver, int port, int side, sure_socket_t *p) {
  // fill the sure_socket_t
  // call udt_init
  int status = udt_init(receiver, port, side, &p->udt);
  if (status == UDT_FAILURE) return SURE_FAILURE;
  p->add = 0, p->num = 0;
  pthread_mutex_init(&p->lock, NULL);

  // start thread (the receiver will need a thread that receives packets and add
  // them to a buffer, the sender will need a thread that receives acks and
  // removes packets from the buffer, or that retransmits if needed) start
  // connection (and wait until it is established)
  int send_status;
  int recv_status;
  sure_packet_t buf_packet;
  if (side == SURE_SENDER) {
    udt_set_timeout(&p->udt, SURE_TIMEOUT);
    // send the syn packet to the receiver :
    sure_packet_t syn_packet = {.seq_ack_number = 0, .flags = SYN};

    for (int i = 0; i < SURE_SYN_TIMEOUT; i++) {
      send_status = udt_send(&p->udt, (char *)&syn_packet, sizeof(syn_packet));
      if (send_status == UDT_FAILURE) return SURE_FAILURE;
      recv_status = udt_recv(&p->udt, (char *)&buf_packet, sizeof(buf_packet));
      if (recv_status == UDT_FAILURE) return SURE_FAILURE;
      if (recv_status >= 0) break;
    }
    // impossible de se connecter
    if (recv_status < 0) {
      fprintf(stderr, "Unable to connect after %d trys \n", SURE_SYN_TIMEOUT);
      return SURE_FAILURE;
    }
    switch (buf_packet.flags) {
      case SYN || ACK:
        printf("The receiver accepted the connextion \n");
        break;
      default:
        fprintf(stderr, "something went wrong when connecting \n");
        return SURE_FAILURE;
    }
    if (pthread_create(&p->thread_id, NULL, sender_thread, (void *)p) != 0) {
      fprintf(stderr, "Unable to create sender_thread thread\n");
      return SURE_FAILURE;
    }
  }
  if (side == SURE_RECEIVER) {
    // waiting for a connextion :
    recv_status = udt_recv(&p->udt, (char *)&buf_packet, sizeof(buf_packet));
    if (recv_status == UDT_FAILURE) return SURE_FAILURE;
    if (buf_packet.flags != SYN) {
      fprintf(stderr, "OOPS something went wrong with the synchronisation  \n");
      return SURE_FAILURE;
    }
    // sends a syn/ack for the synchronized connexion
    sure_packet_t ack_packet = {.seq_ack_number = 0, .flags = ACK || SYN};
    send_status = udt_send(&p->udt, (char *)&ack_packet, sizeof(ack_packet));
    if (pthread_create(&p->thread_id, NULL, receiver_thread, (void *)p) != 0) {
      fprintf(stderr, "Unable to create reveiver_thread thread\n");
      return SURE_FAILURE;
    }
  }
  return SURE_SUCCESS;
}

void sure_close(sure_socket_t *s) {
  // end the connection (and wait until it is done)
  // call udt_close
  // call pthread_join for the thread
}
