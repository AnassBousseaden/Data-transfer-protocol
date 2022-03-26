/* MODIFY THIS FILE
 * together with sure.c, this implements the SURE protocol
 * for reliable data transfer.
 */

#ifndef SURE_H
#define SURE_H

#include <pthread.h>
#include <time.h>

#include "udt.h"

// return codes to be used by SURE functions. DO NOT MODIFY
#define SURE_SUCCESS UDT_SUCCESS
#define SURE_FAILURE UDT_FAILURE

// SURE is a unidirectional protocol, data is sent from sender to receiver only
#define SURE_RECEIVER UDT_RECEIVER
#define SURE_SENDER UDT_SENDER

// this is the size of the payload of SURE packets.
// The whole packet must have at most UDT_PACKET_SIZE,
// but in this limit we must fit the SURE header
//(i.e. whatever else we want to add to sure_packet_t
// MODIFY THIS!!!
#define SURE_PACKET_SIZE UDT_PACKET_SIZE - 8

// the size of the buffer, you can modify this if you want
#define SURE_BUFFER 64  // in number of packets, not bytes

// the size of the sender window, you can modify this if you want
#define SURE_WINDOW 10  // in number of packets, not bytes

// for how long must we wait before assuming the packet was lost?
// you can modify this if you want
#define SURE_TIMEOUT 2000000  // in microseconds

// how many times can we retransmit the fin packet before giving up
// you can modify this if you want
#define SURE_FIN_TIMEOUT 4  // in number of times

// how many times should we try to connect
#define SURE_SYN_TIMEOUT 4

// diffrence in time
#define TIMESPEC_DIFF(t1, t2) \
  ((t2.tv_nsec - t1.tv_nsec) + ((t2.tv_sec - t1.tv_sec) * 1000000000L))

#define TIME_IN_MICRO(tv) (1000000000L * tv.tv_sec + tv.tv_nsec) / 1000L

#define NUMPACKETINWINDOW(p) p->num > SURE_WINDOW ? SURE_WINDOW : p->num

// this is the SURE segment
// MODIFY THIS!!!
typedef struct {
  // ADD HEADER FIELDS HERE
  // Sequence number of ack number (depending on which side is sending)
  unsigned int seq_ack_number;
  unsigned char flags;  // [?|?|?|?|?|ACK|SYN|FIN]
  unsigned short int packet_size;
  char data[SURE_PACKET_SIZE];  // the payload
} sure_packet_t;

// this is the struct the application will provide to all SURE calls,
// it MUST hold all the variables related to the connection (you may
// NOT declare them as global variables), for example the buffer
// MODIFY THIS!!!
typedef struct sure_socket {
  // ADD OTHER VARIABLES HERE
  struct timespec timer;
  pthread_t thread_id;
  int num;                     // number of elements in the buffer
  int start_window;            // start of the window
  int seq_number;              // seq of the next expected packet (for the send)
                               // seq of the current sent packet (for the rev)
  pthread_mutex_t lock;        /* mutex lock for buffer */
  pthread_cond_t empty_buffer; /* condition signaling an empty buffer */
  pthread_cond_t full_buffer;  /* condition signaling a full buffer */
  pthread_cond_t space_buffer; /* condition signaling place in the buffer */
  pthread_cond_t no_empty_buffer; /* condition signaling that the buffer is no
                                     longer empty */
  sure_packet_t buffer[SURE_BUFFER];
  udt_socket_t udt;  // used by the lower-level protocol
} sure_socket_t;

// below are the interface the SURE protocol provides for applications
// DO NO MODIFY!

// initialize our SURE reliable transport using
// the underlying udt unreliable transport service.
// side will be SURE_RECEIVER or SURE_SENDER to
// determine what side we are initializing.
// If side is SURE_RECEIVER, the receiver argument
// will be ignored (it may be NULL)
// A connection will be established during this call (it may block
// for a while)
// This function returns SURE_SUCCESS or
// SURE_FAILURE
int sure_init(char *receiver, int port, int side, sure_socket_t *p);

// ends the SURE transport, doesn't return anything
// in the sender, wait until all the data has been sent before
// closing the connection (it may block for a while)
void sure_close(sure_socket_t *p);

// return SURE_FAILURE or the number of bytes read
int sure_read(sure_socket_t *s, char *msg, int msg_size);
// return SURE_FAILURE or the number of bytes written
// data may just be copied to a buffer and not really written when
// this function returns. If you want to measure the time to send
// data, stop de timer after calling sure_close
int sure_write(sure_socket_t *s, char *msg, int msg_size);

#endif
