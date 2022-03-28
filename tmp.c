/* MODIFY THIS FILE
 * together with sure.c, this implements the SURE protocol
 * for reliable data transfer.
 */
/* The comments in the functions are just hints on what to do,
 * but you are not forced to implement the functions as asked
 * (as long as you respect the interface described in sure.h
 * and that your protocol works)
 */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sure.h"

void printW(sure_socket_t *s) {
  printf("[ ");
  for (int j = s->start_window; j < s->start_window + (NUMPACKETINWINDOW(s));
       j++) {
    printf(" ( %d ) ", s->buffer[j % SURE_BUFFER].seq_ack_number);
    // printf("packet content : ");
    // for (int i = 1; i < s->buffer[j % SURE_BUFFER].packet_size; i++) {
    //   printf("%c", s->buffer[j % SURE_BUFFER].data[i]);
    // }
  }
  printf("]\n");
}

int sure_read(sure_socket_t *s, char *msg, int msg_size) {
  // wait if there isn't anything in the buffer (we'll be signaled by the other
  // thread) if we are not connected, return 0 take as many packets as there are
  // in the buffer and append them into the message that will be returned to the
  // application (respecting the msg_size limitation) return the number of bytes
  // written to msg
  char *tmp = msg;
  int packet_size;
  int total = 0;
  pthread_mutex_lock(&s->lock);
  if (s->num == 0) pthread_cond_wait(&s->filled_buffer, &s->lock);
  if (s->num == 0) return 0;
  while (s->num > 0 && s->buffer[s->start_window].packet_size < msg_size) {
    packet_size = s->buffer[s->start_window].packet_size;
    memcpy(msg, &s->buffer[s->start_window].data, packet_size);
    total = total + packet_size;
    msg_size = msg_size - packet_size;
    msg = &msg[packet_size];
    /* UPDATE the struct */

    printf("taking packet n° (%d) from buffer \n",
           s->buffer[s->start_window].seq_ack_number);
    printf("what is in msg : \n");
    for (int i = 0; i < packet_size; i++) {
      printf("%c", s->buffer[s->start_window].data[i]);
    }
    printf("\n");

    s->num--;
    s->start_window = (s->start_window + 1) % SURE_WINDOW;
  }
  if (s->num == 0) pthread_cond_signal(&s->empty_buffer);
  if (s->num < SURE_BUFFER) pthread_cond_signal(&s->space_buffer);
  pthread_mutex_unlock(&s->lock);
  return (total);
}

int sure_write(sure_socket_t *s, char *msg, int msg_size) {
  // break the application message into multiple SURE packets
  // add them to the buffer (wait if the buffer is full)
  // must do a memory copy because the application buffer may be reused right
  // away send the packets that fall within the window
  /* I have msg[msg_size] */
  int add_index;
  int packet_size;
  int send_status;
  int data_left = msg_size;
  pthread_mutex_lock(&s->lock);
  int i = 0;
  int total = 0;
  while (data_left > 0) {
    i++;
    while (s->num == SURE_BUFFER) pthread_cond_wait(&s->space_buffer, &s->lock);
    /* compute useful values for the packet and the while loop */
    add_index = (s->start_window + s->num) % SURE_BUFFER;
    packet_size =
        data_left < (SURE_PACKET_SIZE) ? data_left : (SURE_PACKET_SIZE);
    data_left = data_left - packet_size;
    total = total + packet_size;
    /* copy data into the buffer's packet dans move the msg pointer */
    memcpy(&(s->buffer[add_index].data), msg, packet_size);
    printf("content of the packet : \n");
    for (int i = 0; i < packet_size; i++) {
      printf("%c", s->buffer[add_index].data[i]);
    }
    printf("\n");
    /* set the header of the packet */
    s->buffer[add_index].ack = false;
    s->buffer[add_index].fin = false;
    s->buffer[add_index].syn = false;
    s->buffer[add_index].packet_size = packet_size;
    s->buffer[add_index].seq_ack_number = s->seq_number;
    printf("writing packet packet n° ( %d ) to buffer \n", s->seq_number);

    /* if num == 0 then set the timer (since we are starting a new frame)*/
    /* we also need  to signal that the buffer is no longer empty */
    if (s->num == 0) {
      pthread_cond_signal(&s->filled_buffer);
      clock_gettime(CLOCK_MONOTONIC, &s->timer);
    }
    // if the packet fall withing the sure window : send the packet
    if (s->num < SURE_WINDOW) {
      send_status = udt_send(&s->udt, (char *)&s->buffer[add_index],
                             sizeof(s->buffer[add_index]));
      if (send_status == UDT_FAILURE) return SURE_FAILURE;
    }
    /* UPDATE */
    msg = &(msg[packet_size]);
    s->seq_number++;
    s->num++;
  }
  printf("number of elements in the buffer after read :%d  \n", s->num);
  printf("we have added :%d elements  \n", i);
  printf("size of the buffer : %d \n", s->num);
  printW(s);

  pthread_mutex_unlock(&s->lock);
  return (total);
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
      printf(
          "recv : [flags : (syn : %d) (ack %d) (fin %d)] [seqence number : %d "
          "]\n",
          packet_recv.syn, packet_recv.ack, packet_recv.fin,
          packet_recv.seq_ack_number);
      pthread_mutex_lock(&p->lock);
      if (packet_recv.syn) {
        // manage the lost of the syn/ack packet
        packet_sent.syn = true;
        packet_sent.ack = true;
        packet_sent.fin = false;
        packet_sent.packet_size = 0;
        packet_sent.seq_ack_number = -1;
        p->seq_number = packet_recv.seq_ack_number;
        udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
        pthread_mutex_unlock(&p->lock);
        continue;
      }
      // the sender wish to end the connexion
      if (packet_recv.fin) {
        // Ending the connection means receiving a FIN packet, responding with
        // an ack and then waiting for some time before tearing down the
        // connection (in case the ack was lost and the sender re-transmits the
        // FIN).
        udt_set_timeout(&p->udt, 4 * SURE_TIMEOUT);
        packet_sent.ack = true;
        packet_sent.fin = true;
        packet_sent.syn = false;
        packet_sent.packet_size = 0;
        packet_sent.seq_ack_number = packet_recv.seq_ack_number + 1;
        // still have to manage errors here
        do {
          udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
          recv_status =
              udt_recv(&p->udt, (char *)&packet_recv, sizeof(packet_recv));

        } while (recv_status != UDT_TIMEOUT);
        pthread_cond_signal(&p->filled_buffer);
        pthread_mutex_unlock(&p->lock);
        return NULL;
      }
      if (packet_recv.ack) {
        fprintf(stderr, "we cannot ack anything soething went wrong \n");
      }
      // here we know we received data therefor we should buffer it.
      // first check if the right packet has arrived to us :
      if (packet_recv.seq_ack_number == p->seq_number) {
        // wait on the buffer to have space
        if (p->num == SURE_BUFFER)
          pthread_cond_wait(&p->space_buffer, &p->lock);
        add_index = (p->start_window + p->num) % SURE_BUFFER;
        p->buffer[add_index] = packet_recv;
        if (p->num == 0) pthread_cond_signal(&p->filled_buffer);
        p->num++;
        p->seq_number++;
        /* send an ack for the received packet with the right seq number */
      }
      // we send a packet telling the sender the next packet we are
      // next are expecting
      packet_sent.ack = true;
      packet_sent.fin = false;
      packet_sent.syn = false;
      packet_sent.seq_ack_number = p->seq_number;
      printf(
          "send : [flags : (syn : %d) (ack %d) (fin %d)] [seqence number : %d "
          "]\n",
          packet_sent.syn, packet_sent.ack, packet_sent.fin,
          packet_sent.seq_ack_number);
      packet_sent.packet_size = 0;
      udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
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
  int fin_cpt = 0;

  while (true) {
    if (fin_cpt > SURE_FIN_TIMEOUT) goto end;
    pthread_mutex_lock(&p->lock);
    // if there is no packets in the buffer we shall wait for the buffer to be
    // filled
    while (p->num == 0) pthread_cond_wait(&p->filled_buffer, &p->lock);

    // retransmitions if needed
    tv = p->timer;
    timer_in_micro = TIME_IN_MICRO(tv);
    clock_gettime(CLOCK_MONOTONIC, &tv);
    current_time = TIME_IN_MICRO(tv);

    // check if there is a timeout
    if (SURE_TIMEOUT < (current_time - timer_in_micro)) {
      // start retransmitting all the window as defined in GO-BACK-N
      // and reset the timer of the frame

      int N = p->num > SURE_WINDOW ? SURE_WINDOW : p->num;
      int send_base = p->start_window;

      // manage the fin_cpt
      if (p->buffer[p->start_window].fin) fin_cpt++;
      printf("retransmitting packets :");
      for (int i = send_base; i < (send_base + N); i++) {
        udt_send(&p->udt, (char *)&p->buffer[i % SURE_BUFFER],
                 sizeof(p->buffer[i % SURE_BUFFER]));
      }
      printW(p);
      clock_gettime(CLOCK_MONOTONIC, &p->timer);
    }
    // if no packet timed out , we can in the  spare time check for acks and
    // move the window (the time we can afford to wait is
    // current_time - current_time)
    udt_set_timeout(&p->udt, current_time - timer_in_micro);
    pthread_mutex_unlock(&p->lock);
    recv_status = udt_recv(&p->udt, (char *)&last_ack, sizeof(last_ack));
    pthread_mutex_lock(&p->lock);
    if (recv_status > 0) {
      printf(
          "recv :[flags : (syn : %d) (ack %d) (fin %d)] [seqence number : %d "
          "]\n",
          last_ack.syn, last_ack.ack, last_ack.fin, last_ack.seq_ack_number);
      /* check  flags  */
      if (p->num == 0) printf("BIG ERROR WITH THE CONDITION \n");

      /* we must end the connexion (we assume that no packet was sent from us
        after the fin.) */
      if (last_ack.fin && last_ack.ack) {
      end:
        if (p->num > 1) {
          fprintf(stderr,
                  "The sender wants to end the connexion but there is still "
                  "packets in the buffer \n");
        }
        pthread_mutex_unlock(&p->lock);
        return NULL;
      }
      int ack_number = last_ack.seq_ack_number;
      if (last_ack.ack && ack_number <= p->seq_number + 1 &&
          ack_number > p->buffer[p->start_window].seq_ack_number) {
        // here we are sure that the window will move thus setting the timer of
        // the new frame
        bool has_moved = false;
        while (p->num > 0) {
          packet = p->buffer[p->start_window];
          // stop condition
          if (ack_number <= packet.seq_ack_number) break;
          // move the window
          has_moved = true;
          // try to send the not yet sent packet at the edge of the window
          if (p->num > SURE_WINDOW) {
            int last_packet_index =
                ((p->start_window) + (SURE_WINDOW)) % SURE_BUFFER;
            udt_send(&p->udt, (char *)&p->buffer[last_packet_index],
                     sizeof(p->buffer[last_packet_index]));
          }
          p->start_window = (p->start_window + 1) % SURE_BUFFER;
          p->num--;
        }
        if (has_moved) clock_gettime(CLOCK_MONOTONIC, &p->timer);
      }
      if (p->num == 0) pthread_cond_signal(&p->empty_buffer);
      if (p->num < SURE_BUFFER) pthread_cond_signal(&p->space_buffer);
    }
    pthread_mutex_unlock(&p->lock);
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
  p->start_window = 0;
  pthread_mutex_init(&p->lock, NULL);
  pthread_mutex_lock(&p->lock);
  pthread_cond_init(&p->empty_buffer, NULL);
  pthread_cond_init(&p->space_buffer, NULL);
  pthread_cond_init(&p->filled_buffer, NULL);

  // start thread (the receiver will need a thread that receives packets and add
  // them to a buffer, the sender will need a thread that receives acks and
  // removes packets from the buffer, or that retransmits if needed) start
  // connection (and wait until it is established)
  int send_status;
  int recv_status;
  sure_packet_t buf_packet;
  if (side == SURE_SENDER) {
    p->sender = true;
    p->receiver = false;
    udt_set_timeout(&p->udt, SURE_TIMEOUT);
    // send the syn packet to the receiver :
    sure_packet_t syn_packet = {.packet_size = 0,
                                .ack = false,
                                .fin = false,
                                .syn = true,
                                .seq_ack_number = p->seq_number};

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

    // check for flags
    if (buf_packet.syn && buf_packet.ack) {
      printf("The receiver accepted the connextion \n");
    } else {
      fprintf(stderr, "something went wrong when connecting \n");
      return SURE_FAILURE;
    }

    if (pthread_create(&p->thread_id, NULL, sender_thread, (void *)p) != 0) {
      fprintf(stderr, "Unable to create sender_thread thread\n");
      return SURE_FAILURE;
    }
  }
  if (side == SURE_RECEIVER) {
    p->sender = false;
    p->receiver = true;
    // waiting for a connextion :
    recv_status = udt_recv(&p->udt, (char *)&buf_packet, sizeof(buf_packet));
    if (recv_status == UDT_FAILURE) return SURE_FAILURE;
    printf("flags : (syn : %d) (ack %d) (fin %d) \n", buf_packet.syn,
           buf_packet.ack, buf_packet.fin);

    if (!buf_packet.syn) {
      fprintf(stderr, "OOPS something went wrong with the synchronisation  \n");
      return SURE_FAILURE;
    }
    p->seq_number = buf_packet.seq_ack_number;
    // sends a syn/ack for the synchronized connexion (if the packet was lost
    // the receiver thread should manage the retransmition)
    sure_packet_t SynAckPacket = {.packet_size = 0,
                                  .ack = true,
                                  .syn = true,
                                  .fin = false,
                                  .seq_ack_number = p->seq_number};
    send_status =
        udt_send(&p->udt, (char *)&SynAckPacket, sizeof(SynAckPacket));
    if (send_status == UDT_FAILURE) return SURE_FAILURE;

    if (pthread_create(&p->thread_id, NULL, receiver_thread, (void *)p) != 0) {
      fprintf(stderr, "Unable to create reveiver_thread thread\n");
      return SURE_FAILURE;
    }
  }
  pthread_cond_signal(&p->empty_buffer);
  pthread_cond_signal(&p->space_buffer);
  pthread_mutex_unlock(&p->lock);
  return SURE_SUCCESS;
}

// end the connection (and wait until it is done)
void sure_close(sure_socket_t *s) {
  // wait for all the data to be sent
  if (s->receiver) {
    pthread_join(s->thread_id, NULL);
    udt_close(&s->udt);
    return;
  }

  pthread_mutex_lock(&s->lock);
  printf("trying to end the connxion \n");
  if (s->num != 0) pthread_cond_wait(&s->empty_buffer, &s->lock);

  // send the fin packet a certain number of times
  sure_packet_t fin_packet = {.seq_ack_number = s->seq_number,
                              .packet_size = 0,
                              .fin = true,
                              .ack = false,
                              .syn = false};

  int add_index = (s->start_window + s->num) % SURE_BUFFER;
  s->buffer[add_index] = fin_packet;
  s->num++;
  s->seq_number++;
  udt_send(&s->udt, (char *)&fin_packet, sizeof(fin_packet));
  pthread_cond_signal(&s->filled_buffer);
  pthread_mutex_unlock(&s->lock);
  // wait for the thread to receive it and end itself
  // call pthread_join for the thread
  printf("waiting for the thread to end \n");
  pthread_join(s->thread_id, NULL);
  printf("the thread ended so we can close the connexion  \n");

  // call udt_close
  udt_close(&s->udt);
}
