/************************************************************************
 * Adapted from a course at Boston University for use in CPSC 317 at UBC
 *
 *
 * The interfaces for the STCP sender (you get to implement them), and a
 * simple application-level routine to drive the sender.
 *
 * This routine reads the data to be transferred over the connection
 * from a file specified and invokes the STCP send functionality to
 * deliver the packets as an ordered sequence of datagrams.
 *
 * Version 2.0
 *
 *
 *************************************************************************/

#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "stcp.h"

#define STCP_SUCCESS 1
#define STCP_ERROR -1

typedef struct {
  int state;
  int fd;
  unsigned int seqNo;
  unsigned int ackNo;
  int timeout;
  int windowSize;
  int sendersPort;
  int receiversPort;
  int inFlight;

  /* YOUR CODE HERE */

} stcp_send_ctrl_blk;
/* ADD ANY EXTRA FUNCTIONS HERE */

void tcpSend(stcp_send_ctrl_blk *cb, int flags, unsigned char *data, int len) {
  int size = sizeof(tcpheader) + len;
  packet pkt;

  createSegment(&pkt, flags, STCP_MAXWIN, cb->seqNo, cb->ackNo, NULL, len);
  pkt.hdr->srcPort = cb->sendersPort;
  pkt.hdr->dstPort = cb->receiversPort;
  memcpy(pkt.data + sizeof(tcpheader), data, len);

  dump('s', &pkt, size);

  htonHdr(pkt.hdr);

  pkt.hdr->checksum = ipchecksum(&pkt, size);

  write(cb->fd, &pkt, size);

  cb->inFlight += size;
}

int tcpReceive(stcp_send_ctrl_blk *cb) {
  packet pkt;
  initPacket(&pkt, NULL, sizeof(packet));

  int readStatus = readWithTimeout(cb->fd, (unsigned char *)&pkt, cb->timeout);
  int packetLength = readStatus;

  if (readStatus == STCP_READ_TIMED_OUT) {
    cb->timeout = stcpNextTimeout(cb->timeout);
    return STCP_READ_TIMED_OUT;
  } else if (readStatus == STCP_READ_PERMANENT_FAILURE) {
    return STCP_READ_PERMANENT_FAILURE;
  }

  ntohHdr(pkt.hdr);

  dump('r', &pkt, packetLength);

  if (pkt.hdr->seqNo <= cb->ackNo) {
    printf("          sender: dropped out of order or duplicate packet %d\n",
           pkt.hdr->seqNo);
    tcpReceive(cb);
  }

  if (getSyn(pkt.hdr) || getFin(pkt.hdr)) {
    cb->ackNo = pkt.hdr->seqNo + 1;
  } else {
    cb->ackNo = pkt.hdr->seqNo + payloadSize(&pkt);
  }

  cb->seqNo = pkt.hdr->ackNo;
  cb->windowSize = pkt.hdr->windowSize;
  cb->timeout = STCP_INITIAL_TIMEOUT;
  cb->inFlight -= packetLength;

  return packetLength;
}

/*
 * Send STCP. This routine is to send all the data (len bytes).  If more
 * than MSS bytes are to be sent, the routine breaks the data into multiple
 * packets. It will keep sending data until the send window is full or all
 * the data has been sent. At which point it reads data from the network to,
 * hopefully, get the ACKs that open the window. You will need to be careful
 * about timing your packets and dealing with the last piece of data.
 *
 * Your sender program will spend almost all of its time in either this
 * function or in tcp_close().  All input processing (you can use the
 * function readWithTimeout() defined in stcp.c to receive segments) is done
 * as a side effect of the work of this function (and stcp_close()).
 *
 * The function returns STCP_SUCCESS on success, or STCP_ERROR on error.
 */
int stcp_send(stcp_send_ctrl_blk *stcp_CB, unsigned char *data, int length) {
  /* YOUR CODE HERE */

  for (int i = 0; i < length; i += STCP_MSS) {
    tcpSend(stcp_CB, ACK, data + i, min(STCP_MSS, length - i));
    int ackStatus = tcpReceive(stcp_CB);
    if (ackStatus == STCP_READ_TIMED_OUT) {
      i -= STCP_MSS;
    } else if (ackStatus == STCP_READ_PERMANENT_FAILURE) {
      return STCP_ERROR;
    }
  }

  stcp_CB->state = STCP_SENDER_CLOSING;

  return STCP_SUCCESS;
}

/*
 * Open the sender side of the STCP connection. Returns the pointer to
 * a newly allocated control block containing the basic information
 * about the connection. Returns NULL if an error happened.
 *
 * If you use udp_open() it will use connect() on the UDP socket
 * then all packets then sent and received on the given file
 * descriptor go to and are received from the specified host. Reads
 * and writes are still completed in a datagram unit size, but the
 * application does not have to do the multiplexing and
 * demultiplexing. This greatly simplifies things but restricts the
 * number of "connections" to the number of file descriptors and isn't
 * very good for a pure request response protocol like DNS where there
 * is no long term relationship between the client and server.
 */
stcp_send_ctrl_blk *stcp_open(char *destination,
                              int sendersPort,
                              int receiversPort) {
  logLog("init",
         "Sending from port %d to <%s, %d>",
         sendersPort,
         destination,
         receiversPort);
  // Since I am the sender, the destination and receiversPort name the other
  // side
  int fd = udp_open(destination, receiversPort, sendersPort);

  stcp_send_ctrl_blk *cb = malloc(sizeof(stcp_send_ctrl_blk));
  cb->state = STCP_SENDER_CLOSED;
  cb->seqNo = rand() % 4294967296;
  cb->ackNo = 0;
  cb->fd = fd;
  cb->timeout = STCP_INITIAL_TIMEOUT;
  cb->windowSize = STCP_MAXWIN;
  cb->sendersPort = sendersPort;
  cb->receiversPort = receiversPort;
  cb->inFlight = 0;

  while (1) {
    tcpSend(cb, SYN, NULL, 0);

    cb->state = STCP_SENDER_SYN_SENT;

    int ackStatus = tcpReceive(cb);
    if (ackStatus == STCP_READ_TIMED_OUT) {
      continue;
    } else if (ackStatus == STCP_READ_PERMANENT_FAILURE) {
      return NULL;
    } else {
      break;
    }
  }

  cb->state = STCP_SENDER_ESTABLISHED;

  return cb;
}

/*
 * Make sure all the outstanding data has been transmitted and
 * acknowledged, and then initiate closing the connection. This
 * function is also responsible for freeing and closing all necessary
 * structures that were not previously freed, including the control
 * block itself.
 *
 * Returns STCP_SUCCESS on success or STCP_ERROR on error.
 */
int stcp_close(stcp_send_ctrl_blk *cb) {
  /* YOUR CODE HERE */
  tcpSend(cb, FIN | ACK, NULL, 0);

  cb->state = STCP_SENDER_FIN_WAIT;

  tcpReceive(cb);

  tcpSend(cb, ACK, NULL, 0);

  cb->state = STCP_SENDER_CLOSED;

  return STCP_SUCCESS;
}
/*
 * Return a port number based on the uid of the caller.  This will
 * with reasonably high probability return a port number different from
 * that chosen for other uses on the undergraduate Linux systems.
 *
 * This port is used if ports are not specified on the command line.
 */
int getDefaultPort() {
  uid_t uid = getuid();
  int port = (uid % (32768 - 512) * 2) + 1024;
  assert(port >= 1024 && port <= 65535 - 1);
  return port;
}

/*
 * This application is to invoke the send-side functionality.
 */
int main(int argc, char **argv) {
  stcp_send_ctrl_blk *cb;

  char *destinationHost;
  int receiversPort, sendersPort;
  char *filename = NULL;
  int file;
  /* You might want to change the size of this buffer to test how your
   * code deals with different packet sizes.
   */
  unsigned char buffer[STCP_MSS];
  int num_read_bytes;

  logConfig("sender", "init,segment,error,failure");
  /* Verify that the arguments are right */
  if (argc > 5 || argc == 1) {
    fprintf(stderr,
            "usage: sender DestinationIPAddress/Name receiveDataOnPort "
            "sendDataToPort filename\n");
    fprintf(stderr, "or   : sender filename\n");
    exit(1);
  }
  if (argc == 2) {
    filename = argv[1];
    argc--;
  }

  // Extract the arguments
  destinationHost = argc > 1 ? argv[1] : "localhost";
  receiversPort = argc > 2 ? atoi(argv[2]) : getDefaultPort();
  sendersPort = argc > 3 ? atoi(argv[3]) : getDefaultPort() + 1;
  if (argc > 4) filename = argv[4];

  /* Open file for transfer */
  file = open(filename, O_RDONLY);
  if (file < 0) {
    logPerror(filename);
    exit(1);
  }

  /*
   * Open connection to destination.  If stcp_open succeeds the
   * control block should be correctly initialized.
   */
  cb = stcp_open(destinationHost, sendersPort, receiversPort);
  if (cb == NULL) {
    /* YOUR CODE HERE */
    printf("Error opening connection\n");
  }

  /* Start to send data in file via STCP to remote receiver. Chop up
   * the file into pieces as large as max packet size and transmit
   * those pieces.
   */
  while (1) {
    num_read_bytes = read(file, buffer, sizeof(buffer));

    /* Break when EOF is reached */
    if (num_read_bytes <= 0) break;

    if (stcp_send(cb, buffer, num_read_bytes) == STCP_ERROR) {
      /* YOUR CODE HERE */
      printf("Error sending data\n");
    }
  }

  /* Close the connection to remote receiver */
  if (stcp_close(cb) == STCP_ERROR) {
    /* YOUR CODE HERE */
  }

  return 0;
}
