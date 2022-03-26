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

int sure_read(sure_socket_t *s, char *msg, int msg_size) {
  // wait if there isn't anything in the buffer (we'll be signaled by the other
  // thread) if we are not connected, return 0 take as many packets as there are
  // in the buffer and append them into the message that will be returned to the
  // application (respecting the msg_size limitation) return the number of bytes
  // written to msg
  pthread_mutex_lock(&s->lock);
}

int sure_write(sure_socket_t *s, char *msg, int msg_size) {
  // break the application message into multiple SURE packets
  // add them to the buffer (wait if the buffer is full)
  // must do a memory copy because the application buffer may be reused right
  // away send the packets that fall within the window
  /* I have msg[msg_size] */
  int add_index;
  int packet_size;
  int data_left = msg_size;
  /* N : the number of packet we need to send */
  pthread_mutex_lock(&s->lock);
  while (data_left > 0) {
    while (s->num == SURE_BUFFER) {
      pthread_cond_signal(&s->full_buffer);
      pthread_cond_wait(&s->space_buffer, &s->lock);
    }
    /* compute useful values for the packet and the while loop */
    add_index = (s->start_window + s->num) % SURE_BUFFER;
    packet_size = data_left % SURE_PACKET_SIZE;
    data_left = data_left - packet_size;
    /* copy data into the buffer's packet dans move the msg pointer */
    memcpy(&s->buffer[add_index].data, msg, packet_size);
    msg = &msg[packet_size];
    /* set the header of the packet */
    s->buffer[add_index].flags = 0;
    s->buffer[add_index].packet_size = packet_size;
    s->buffer[add_index].seq_ack_number = &s->seq_number;
    // if the packet fall withing the sure window : send the packet

    /* if num == 0 then set the timer (since we are starting a new frame)*/
    /* we also need  to signal that the buffer is no longer empty */
    if (s->num == 0) {
      clock_gettime(CLOCK_MONOTONIC, &s->timer);
    }

    /* UPDATE the struct */
    s->seq_number++;
    s->num++;
  }
  pthread_mutex_unlock(&s->lock);
}
// thread that receive packets and add them to the buffer
void *receiver_thread(sure_socket_t *p) {
  int recv_status;
  sure_packet_t packet_recv;
  sure_packet_t packet_sent;
  int send_status;
  int add_index;
  while (true) {
    recv_status = udt_recv(&p->udt, (char *)&packet_recv, sizeof(packet_recv));
    if (recv_status > 0) {
      pthread_mutex_lock(&p->lock);
      if ((packet_recv.flags | SYN) == 0) {
        // manage the lost of the syn/ack packet
        packet_sent.flags = (SYN | ACK);
        packet_sent.seq_ack_number = packet_recv.seq_ack_number + 1;
        udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
      }
      // the sender wish to end the connexion
      if ((packet_recv.flags | FIN) == 0) {
        // Ending the connection means receiving a FIN packet, responding with
        // an ack and then waiting for some time before tearing down the
        // connection (in case the ack was lost and the sender re-transmits the
        // FIN).
        udt_set_timeout(&p->udt, SURE_TIMEOUT);
        packet_sent.flags = (ACK | FIN);
        do {
          recv_status =
              udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
        } while (recv_status != 3 * UDT_TIMEOUT);
        return NULL;
      }
      // here we know we received data therefor we should buffer it.
      // first check if the right packet has arrived to us :
      if (packet_recv.seq_ack_number == p->seq_number) {
        // wait on the buffer to have space
        pthread_cond_wait(&p->space_buffer, &p->lock);
        add_index = (p->start_window + p->num) % SURE_BUFFER;
        p->buffer[add_index] = packet_recv;
        p->num++;
        /* send an ack for the received packet with the right seq number */
        p->seq_number++;
      }
      // we send a packet telling the sender the next packet we
      // are expecting
      packet_sent.flags = ACK;
      packet_sent.seq_ack_number = p->seq_number;
      packet_sent.packet_size = 0;
      udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
      if (p->num == SURE_BUFFER) pthread_cond_signal(&p->full_buffer);
      pthread_mutex_unlock(&p->lock);
    }
  }
}

// thread that  receives acks and removes packets from the buffer, or that
// retransmits if needed
void *sender_thread(sure_socket_t *p) {
  // sure_packet_t buf_packet;
  int recv_status;
  int send_status;
  sure_packet_t last_ack;
  sure_packet_t packet;
  unsigned long timer_in_micro;
  unsigned long current_time;
  struct timespec tv;
  int FIN_cpt = 0;

  while (true) {
    pthread_mutex_lock(&p->lock);
    // if there is no packets in the buffer we shall wait for the buffer to be
    // filled
    while (p->num == 0) pthread_cond_wait(&p->filled_buffer, &p->lock);

    if (FIN_cpt >= SURE_FIN_TIMEOUT) goto end;
    // retransmitions if needed
    tv = p->timer;
    timer_in_micro = TIME_IN_MICRO(tv);
    clock_gettime(CLOCK_MONOTONIC, &tv);
    current_time = TIME_IN_MICRO(tv);

    // check if there is a timeout
    if (current_time - timer_in_micro >= SURE_TIMEOUT) {
      // start retransmitting all the window as defined in GO-BACK-N
      // and reset the timer of the frame
      int N = NUMPACKETINWINDOW(p);
      int send_base = p->start_window;

      // manage the fin_cpt
      if ((p->buffer[send_base].flags | FIN) == 0) {
        FIN_cpt++;
      }

      for (int i = send_base; i < send_base + N; i++)
        udt_send(&p->udt, (char *)&p->buffer[p->start_window],
                 sizeof(p->buffer[p->start_window]));
      clock_gettime(CLOCK_MONOTONIC, &p->timer);
    }
    pthread_mutex_unlock(&p->lock);

    // if no packet timed out , we can in the  spare time check for acks and
    // move the window (the time we can afford to wait is
    // current_time - current_time)
    udt_set_timeout(&p->udt, current_time - timer_in_micro);
    recv_status = udt_recv(&p->udt, (char *)&last_ack, sizeof(last_ack));
    if (recv_status > 0) {
      // CHECK FOR flags (FIN  , ACK)
      if ((last_ack.flags | FIN) == 0) {
        if (p->num > 0) {
          fprintf(stderr,
                  "The sender wants to end the connexion but there is still "
                  "packets in the buffer \n");
        }
        return NULL;
      }
      if ((last_ack.flags | ACK) == 0) {
        unsigned int ack_number = last_ack.seq_ack_number;
        // here we are sure that the window will move thus setting the timer of
        // the new frame
        clock_gettime(CLOCK_MONOTONIC, &p->timer);
        while (true) {
          packet = p->buffer[p->start_window];
          // stop condition
          if (ack_number <= packet.seq_ack_number) break;
          // move the window
          p->start_window = (p->start_window + 1) % SURE_BUFFER;
          // try to send the not yet sent packet at the edge of the window
          if (p->num > SURE_WINDOW) {
            int last_packet_index =
                (p->start_window + SURE_WINDOW - 1) % SURE_BUFFER;
            udt_send(&p->udt, (char *)&p->buffer[last_packet_index],
                     sizeof(p->buffer[last_packet_index]));
          }
          // update the buffer
          p->num--;
        }
        if (p->num == 0) pthread_cond_signal(&p->empty_buffer);
        if (p->num < SURE_BUFFER) pthread_cond_signal(&p->space_buffer);
        pthread_mutex_unlock(&p->lock);
      }
      // we must end the connexion (we assume that no packet was sent from us
      // after the fin.)
    }
  }
  return NULL;
}

int sure_init(char *receiver, int port, int side, sure_socket_t *p) {
  // fill the sure_socket_t
  // call udt_init
  int status = udt_init(receiver, port, side, &p->udt);
  if (status == UDT_FAILURE) return SURE_FAILURE;
  p->num = 0;
  p->seq_number = 0;
  pthread_mutex_init(&p->lock, NULL);
  pthread_cond_init(&p->empty_buffer, NULL);
  pthread_cond_init(&p->full_buffer, NULL);
  pthread_cond_init(&p->space_buffer, NULL);

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
    sure_packet_t syn_packet = {.seq_ack_number = p->seq_number, .flags = SYN};

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
    // increase the seq_number since a packet has been sent and received
    p->seq_number++;
    // check for flags
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
    // sends a syn/ack for the synchronized connexion (if the packet was lost
    // the receiver thread should manage the retransmition)
    sure_packet_t ack_packet = {.seq_ack_number = buf_packet.seq_ack_number + 1,
                                .flags = (ACK | SYN)};
    send_status = udt_send(&p->udt, (char *)&ack_packet, sizeof(ack_packet));
    if (send_status == UDT_FAILURE) return SURE_FAILURE;

    if (pthread_create(&p->thread_id, NULL, receiver_thread, (void *)p) != 0) {
      fprintf(stderr, "Unable to create reveiver_thread thread\n");
      return SURE_FAILURE;
    }
  }
  return SURE_SUCCESS;
}

// end the connection (and wait until it is done)
void sure_close(sure_socket_t *s) {
  // wait for all the data to be sent
  pthread_mutex_lock(&s->lock);
  pthread_cond_wait(&s->empty_buffer, &s->lock);
  // send the fin packet a certain number of times
  sure_packet_t fin_packet = {.seq_ack_number = s->seq_number, .flags = (FIN)};
  int add_index = (s->start_window + s->num) % SURE_BUFFER;
  s->buffer[add_index] = fin_packet;
  s->num++;
  udt_send(&s->udt, (char *)&fin_packet, sizeof(fin_packet));
  pthread_mutex_unlock(&s->lock);
  // wait for the thread to receive it and end itself
  // call pthread_join for the thread
  pthread_join(s->thread_id, NULL);
  // call udt_close
  udt_close(&s->udt);
}
