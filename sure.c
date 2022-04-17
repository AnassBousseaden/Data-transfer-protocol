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

void printW(sure_socket_t *s) {
  printf("[ ");
  for (int j = s->start_window; j < s->start_window + (NUMPACKETINWINDOW(s));
       j++) {
    printf(" ( %d ) ", s->buffer[j % SURE_BUFFER].seq_ack_number);
    printf("packet content : ");
    for (int i = 1; i < s->buffer[j % SURE_BUFFER].packet_size; i++) {
      printf("%c", s->buffer[j % SURE_BUFFER].data[i]);
    }
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
  /* manage the fin condition */
  if (s->buffer[s->start_window].fin) {
    pthread_mutex_unlock(&s->lock);
    return 0;
  }
  while (s->num > 0 && (s->buffer[s->start_window].fin == false) &&
         s->buffer[s->start_window].packet_size < msg_size) {
    packet_size = s->buffer[s->start_window].packet_size;
    memcpy(msg, &s->buffer[s->start_window].data, packet_size);
    total = total + packet_size;
    msg_size = msg_size - packet_size;
    msg = &msg[packet_size];
    /* UPDATE the struct */
    s->num--;
    s->start_window = (s->start_window + 1) % SURE_BUFFER;
  }
  printf("number of elements in the buffer : (%d) \n", s->num);
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
  if ((s->num == 0) && (msg_size > 0)) pthread_cond_signal(&s->filled_buffer);

  while (data_left > 0) {
    while (s->num == SURE_BUFFER) pthread_cond_wait(&s->space_buffer, &s->lock);
    /* compute useful values for the packet and the while loop */
    add_index = (s->start_window + s->num) % SURE_BUFFER;
    packet_size =
        data_left < (SURE_PACKET_SIZE) ? data_left : (SURE_PACKET_SIZE);
    /* copy data into the buffer's packet dans move the msg pointer */
    memcpy(&(s->buffer[add_index].data), msg, packet_size);
    s->buffer[add_index].ack = false;
    s->buffer[add_index].fin = false;
    s->buffer[add_index].syn = false;
    s->buffer[add_index].packet_size = packet_size;
    s->buffer[add_index].seq_ack_number = s->seq_number;
    printf("writing packet n° ( %d ) in the buffer \n", s->seq_number);
    clock_gettime(CLOCK_MONOTONIC, &s->timers[add_index]);
    // if the packet fall withing the sure window : send the packet
    if (s->num < SURE_WINDOW) {
      send_status = udt_send(&s->udt, (char *)&s->buffer[add_index],
                             sizeof(s->buffer[add_index]));
      if (send_status == UDT_FAILURE) return SURE_FAILURE;
    }
    /* UPDATE */
    data_left = data_left - packet_size;
    msg = &(msg[packet_size]);
    s->seq_number++;
    s->num++;
  }
  printf("number of elements in the buffer after read :%d  \n\n", s->num);
  // printf("we have added :%d elements  \n", i);
  // printf("size of the buffer : %d \n", s->num);
  // printW(s);
  pthread_mutex_unlock(&s->lock);
  return (msg_size - data_left);
}

// thread that receive packets and add them to the buffer
void *receiver_thread(sure_socket_t *p) {
  // useful variables
  int recv_status;
  sure_packet_t packet_recv;
  sure_packet_t packet_sent;
  int add_index;
  while (true) {
    recv_status = udt_recv(&p->udt, (char *)&packet_recv, sizeof(packet_recv));
    if (recv_status <= 0) continue;

    /* HERE (recv_status > 0) so we have data */

    pthread_mutex_lock(&p->lock);
    if (packet_recv.syn) {
      // manage the lost of the syn/ack packet
      packet_sent.syn = true;
      packet_sent.ack = true;
      packet_sent.fin = false;
      packet_sent.packet_size = 0;
      // the syn packet does not have a seq number its a special packet
      packet_sent.seq_ack_number = -1;
      p->seq_number = packet_recv.seq_ack_number;
      udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
      pthread_mutex_unlock(&p->lock);
      continue;
    }
    // the sender wish to end the connexion
    if (packet_recv.fin && packet_recv.seq_ack_number == p->seq_number) {
      udt_set_timeout(&p->udt, 3 * (SURE_TIMEOUT / 2));
      packet_sent.ack = true;
      packet_sent.fin = true;
      packet_sent.syn = false;
      packet_sent.packet_size = 0;
      packet_sent.seq_ack_number = packet_recv.seq_ack_number + 1;
      // buffer the packet to tell read we are ending the file transfer
      add_index = (p->start_window + p->num) % SURE_BUFFER;
      p->buffer[add_index] = packet_recv;
      p->num++;
      // still have to manage errors here
      recv_status = 0;
      for (int i = 0; i < SURE_FIN_TIMEOUT && recv_status != UDT_TIMEOUT; i++) {
        udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
        recv_status =
            udt_recv(&p->udt, (char *)&packet_recv, sizeof(packet_recv));
      }
      printf("the recv thread is ending with %d elements in the buffer \n\n",
             p->num);
      pthread_cond_signal(&p->filled_buffer);
      pthread_mutex_unlock(&p->lock);
      return NULL;
    }
    // we received data so lets check if its the packet we are expecting
    if (packet_recv.seq_ack_number == p->seq_number) {
      if (p->num == SURE_BUFFER) pthread_cond_wait(&p->space_buffer, &p->lock);
      add_index = (p->start_window + p->num) % SURE_BUFFER;
      p->buffer[add_index] = packet_recv;
      if (p->num == 0) pthread_cond_signal(&p->filled_buffer);
      p->num++;
      p->seq_number++;
    }
    // we send a packet telling the sender the next packet we are
    // next are expecting

    printf("receved packet number : ( %d ) , sending ack number : ( %d ) \n",
           packet_recv.seq_ack_number, p->seq_number);
    packet_sent.ack = true;
    packet_sent.fin = false;
    packet_sent.syn = false;
    packet_sent.seq_ack_number = p->seq_number;
    packet_sent.packet_size = 0;
    udt_send(&p->udt, (char *)&packet_sent, sizeof(packet_sent));
    pthread_mutex_unlock(&p->lock);
  }
}

// thread that  receives acks and removes packets from the buffer, or that
// retransmits if needed
void *sender_thread(sure_socket_t *p) {
  // sure_packet_t buf_packet;
  int recv_status;
  int send_status;
  sure_packet_t recv_packet;
  sure_packet_t packet;
  unsigned long timer_micro;
  unsigned long current_time;
  struct timespec tv;
  int cpt_fin = 0;

  while (true) {
    /*end the thread if the fin packet times out SURE_FIN_TIMEOUT times */
    if (cpt_fin >= SURE_FIN_TIMEOUT) goto end;
    pthread_mutex_lock(&p->lock);
    /* if there is no packets in the buffer we shall wait for the buffer to be
     * filled */
    while (p->num == 0) pthread_cond_wait(&p->filled_buffer, &p->lock);

    // retransmitions if needed
    tv = p->timers[p->start_window];
    timer_micro = TIME_IN_MICRO(tv);
    clock_gettime(CLOCK_MONOTONIC, &tv);
    current_time = TIME_IN_MICRO(tv);
    // check if there is a timeout
    if (SURE_TIMEOUT <= (current_time - timer_micro)) {
      // start retransmitting all the window as defined in GO-BACK-N
      // and reset the timers of the packets in the window
      int n_window = p->num > SURE_WINDOW
                         ? SURE_WINDOW
                         : p->num;     /* number of elements in the window */
      int send_base = p->start_window; /* where the window starts  */
      // increment the cpt_fin if the fin packet times out
      if (p->buffer[p->start_window].fin) cpt_fin++;

      printf("retransmitting packets \n\n");
      printf("|");
      for (int i = send_base; i < (send_base + n_window); i++) {
        printf(" %d |", p->buffer[i % SURE_BUFFER].seq_ack_number);
        udt_send(&p->udt, (char *)&p->buffer[i % SURE_BUFFER],
                 sizeof(p->buffer[i % SURE_BUFFER]));
        clock_gettime(CLOCK_MONOTONIC, &p->timers[i % SURE_BUFFER]);
      }
      printf("\n");
      // recalculate the timers since we just changed them (this is overkill and
      // can be improved if performance is important)
      tv = p->timers[p->start_window];
      timer_micro = TIME_IN_MICRO(tv);
      clock_gettime(CLOCK_MONOTONIC, &tv);
      current_time = TIME_IN_MICRO(tv);
    }

    // if no packet timed out , we can in the  spare time check for acks and
    // move the window (the time we can afford to wait is
    // current_time - current_time)

    pthread_mutex_unlock(&p->lock);
    udt_set_timeout(&p->udt, current_time - timer_micro);
    recv_status = udt_recv(&p->udt, (char *)&recv_packet, sizeof(recv_packet));
    if (recv_status <= 0) continue;

    pthread_mutex_lock(&p->lock);
    /* HERE (recv_status > 0) so we have data */

    /* we must end the connexion (we assume that no packet was sent from us
      after the fin.) */
    if (recv_packet.fin && recv_packet.ack &&
        recv_packet.seq_ack_number == p->seq_number) {
    end:
      // move the window but we don't need to
      printf("ended the connexion after %d retransmit of the fin packet\n",
             cpt_fin);
      p->num--;
      p->start_window = (p->start_window + 1) % SURE_BUFFER;
      pthread_mutex_unlock(&p->lock);
      return NULL;
    }
    int recv_ack_n = recv_packet.seq_ack_number;  // ack number
    /* Move the window (or not ) */
    if (recv_packet.ack) {
      while (p->num > 0) {
        packet = p->buffer[p->start_window];
        // stop condition
        if (recv_ack_n <= packet.seq_ack_number) break;
        if (p->num > SURE_WINDOW) {
          int edgePacketIndex = (p->start_window + SURE_WINDOW) % SURE_BUFFER;
          udt_send(&p->udt, (char *)&p->buffer[edgePacketIndex],
                   sizeof(p->buffer[edgePacketIndex]));
          // set the timer
          clock_gettime(CLOCK_MONOTONIC, &p->timers[edgePacketIndex]);
        }
        p->start_window = (p->start_window + 1) % SURE_BUFFER;
        p->num--;
      }
    }
    if (p->num == 0) pthread_cond_signal(&p->empty_buffer);
    if (p->num < SURE_BUFFER) pthread_cond_signal(&p->space_buffer);
    pthread_mutex_unlock(&p->lock);
  }
  return NULL;
}

int sure_init(char *receiver, int port, int side, sure_socket_t *p) {
  // fill the sure_socket_t
  // call udt_init
  int status = udt_init(receiver, port, side, &(p->udt));
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
  clock_gettime(CLOCK_MONOTONIC, &s->timers[add_index]);
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
