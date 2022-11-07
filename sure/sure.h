#ifndef SURE_H
#define SURE_H

#include <pthread.h>
#include <time.h>

#include "udt.h"

// return codes to be used by SURE functions.
#define SURE_SUCCESS UDT_SUCCESS
#define SURE_FAILURE UDT_FAILURE

// SURE is a unidirectional protocol, data is sent from sender to receiver only
#define SURE_RECEIVER UDT_RECEIVER
#define SURE_SENDER UDT_SENDER

#define SURE_HEADER_SIZE 12
// this is the size of the payload of SURE packets.
// The whole packet must have at most UDT_PACKET_SIZE,
#define SURE_PACKET_SIZE (UDT_PACKET_SIZE - SURE_HEADER_SIZE)

// the size of the buffer
#define SURE_BUFFER 64  // in number of packets, not bytes

// the size of the sender window
#define SURE_WINDOW 35  // in number of packets, not bytes

// for how long must we wait before assuming the packet was lost?
#define SURE_TIMEOUT 2000000  // in microseconds

// how many times can we retransmit the fin packet before giving up
#define SURE_FIN_TIMEOUT 4  // in number of times

// how many times should we try to connect
#define SURE_SYN_TIMEOUT 4

// MACRO to calculate diffrence in time
#define TIMESPEC_DIFF(t1, t2) \
  ((t2.tv_nsec - t1.tv_nsec) + ((t2.tv_sec - t1.tv_sec) * 1000000000L))

// MACRO to convert the time value to micro seconds
#define TIME_IN_MICRO(tv) (1000000000L * tv.tv_sec + tv.tv_nsec) / 1000L

// macro to get the number of packets in the window
#define NUMPACKETINWINDOW(p) p->num > SURE_WINDOW ? SURE_WINDOW : p->num

typedef struct {
  // HEADER FIELDS HERE
  // Sequence number of Acknowledgement number (depending on which side is
  // sending)
  int seq_ack_number;
  bool syn;
  bool ack;
  bool fin;
  int packet_size;
  char data[SURE_PACKET_SIZE];  // the payload
} sure_packet_t;

// this is the struct the application will provide to all SURE calls,
typedef struct {
  bool receiver;
  struct timespec timers[SURE_BUFFER]; /* timer for each packet in the window */
  pthread_t thread_id;
  int num;                      // number of elements in the buffer
  int start_window;             // start of the window
  int seq_number;               // next available sequence number (for the send)
                                // seq of the current sent packet (for the rev)
  pthread_mutex_t lock;         /* mutex lock for common resources */
  pthread_cond_t empty_buffer;  /* condition signaling an empty buffer */
  pthread_cond_t space_buffer;  /* condition signaling place in the buffer */
  pthread_cond_t filled_buffer; /* condition signaling that the buffer  is no
                                   longer empty */
  sure_packet_t buffer[SURE_BUFFER];
  udt_socket_t udt;  // used by the lower-level protocol
} sure_socket_t;

// SENDER :
// need a condtion waiting for the buffer to be filled
// need a lock on (BUFFER , NUM)

// below are the interface the SURE protocol provides for applications

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
