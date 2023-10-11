/* 
 * File:   sender_main.c
 * Author: farazms2, nvk4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>

#define MAXDATASIZE 500000
#define PAYLOADSIZE 500
#define CONTROLBITLENGTH 12

struct sockaddr_in si_other;
int s, slen;
struct sockaddr addr;
socklen_t fromlen = sizeof(addr);

void diep(char *s) {
    perror(s);
    exit(1);
}


double _ceil(double num) {
    int integerPart = (int)num;
    double decimalPart = num - integerPart;
    
    if (decimalPart > 0) {
        return integerPart + 1;
    } else {
        return integerPart;
    }
}


int min(int a, int b) {
    return (a < b) ? a : b;
}

/*
Reliable data transfer plan TCP sender
Data Structures
SST = 100 (50,000 byte mark) or 64
Duplicate ACK counter 
DUplicate ACK counter threshold = 10 or 3
Highest ACK packet sequence number (HACK)
Congestion window of size 1 of TCP struct 
File data array with 500 bytes per index 
Timeout threshold = 5 RTTS (100 ms)
ENUM for sender state: Slow start, Congestion avoidance, Fast recovery
TCP struct:
    Sequence number 
    Data payload index into file data array
    Timer 
Algorithmns 
UNIVERSAL
Iterate through window, if timer not started, populate sequence number, data payload index, send packets
and then start timer. 

SLOW START 
Recieve new ACK: Check if ACK sequence number is higher than HACK, next expand congestion window by 
current congestion window size + highest ACK seen - new ACK sequence number, reset HACK, mark 
all packets with sequence number <= newest ACK as completed and shift congestion window. Reset duplicate
ACK counter to zero. If congestion window size reaches SST then switch to congestion window state.
Recieve duplicate ACK: Check if ACK sequence number is <= HACK and increment duplicate ACK counter,
If duplicate ACK counter reaches threshold switch to Fast recovery state. Then, Set SST equal to CW/2.
Next,set Congestion window size and expand or contract window equal to SST + duplicate ACK count. 

CONGESTION AVOIDANCE 
Recieve new ACK: Check if ACK sequence number is higher than HACK, next expand congestion window by 
current congestion window size + 1/floor(congestion window size) and repeat (highest ACK seen - new ACK 
sequence number) times. Set highest ACK seen and mark all packets with sequence number <= newest ACK
as completed and shift congestion window. Set duplicate ACK counter to zero.
Recieve duplicate ACK: Same as process in slow start. 

FAST RECOVERY
Recieve new ACK: Check if ACK sequence number is higher than HACK, next expand congestion window to SST.
Set highest ACK seen and mark all packets with sequence number <= newest ACK as completed and shift congestion window. Set duplicate ACK counter to zero.
Recieve duplicate ACK: Check if ACK sequence number is <= HACK and increment duplicate ack counter. Next,
Next expand congestion window by 1. 

UNIVERSAL
Timeout: Check if any packet in window has reached timer threshold, set SST to be half of CW, truncate CW to be 1,
set duplicate ACK counter to zero and set mode to SLOW START. Clear timer of only packet remaining.  
*/

/*
Packets have sequence number
Send SYN packet
Wait for SYN/ACK packet for 1 second else go back to previous step 
Send ACK packet 
Wait for a SYN/ACK packet for 1 second else go back to previous step
*/
void SYNFIN(char *chunk_buf, int totalBytesChunk, bool SYNMessage) {
    int currentBytesSent = 0;
    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    if(SYNMessage) {
        sprintf(chunk_buf, "%s", "SYN");
    }
    else {
        sprintf(chunk_buf, "%s", "FIN");
    }

    while(currentBytesSent != totalBytesChunk) {
        if((currentBytesSent += sendto(s, &chunk_buf[currentBytesSent], totalBytesChunk -currentBytesSent, 0, (struct sockaddr *) &si_other, slen)) == -1) {
            perror("chunk sending failure.\n");
            exit(1);
        }
    }
}

bool ACKSYNFIN(char *chunk_buf, int totalBytesChunk, bool SYNMessage, bool finalCheck) {
    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    bool ACKReceived = false;

    while(!ACKReceived) {
        int currentBytesRecieved = 0;
        while(currentBytesRecieved != totalBytesChunk) {
            if((currentBytesRecieved += recvfrom(s, &chunk_buf[currentBytesRecieved], totalBytesChunk -currentBytesRecieved, 0, &addr, &fromlen)) == -1) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                    // handle timeout
                    printf("recvfrom() timed out\n");
                    ACKReceived = false;
                    break;
                } else {
                    // handle other errors
                    perror("chunk sending failure.\n");
                    exit(1);
                }
            }
        }
        //parsing response
        if(SYNMessage) {
            if(strstr(chunk_buf, "SYN/ACK") != NULL) {
                ACKReceived = true;
            }
        }
        else {
            if(strstr(chunk_buf, "FIN/ACK") != NULL) {
                ACKReceived = true;
            }
        }

        //next steps go back to sending SYN/FIN or decide to re-send ACK
        if(!finalCheck && !ACKReceived) {
            SYNFIN(chunk_buf, totalBytesChunk, SYNMessage);
        }
        else if(finalCheck && ACKReceived) {
            return true;
        }
        if(finalCheck && !ACKReceived) {
            return false;
        }
    }
    return true;
}

void ACK(char *chunk_buf, int totalBytesChunk, bool SYNMessage) {
    do {
        int currentBytesSent = 0;
        memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
        sprintf(chunk_buf, "%s", "ACK");
        while(currentBytesSent != totalBytesChunk) {
            if((currentBytesSent += sendto(s, &chunk_buf[currentBytesSent], totalBytesChunk -currentBytesSent, 0, (struct sockaddr *) &si_other, slen)) == -1) {
                perror("chunk sending failure.\n");
                exit(1);
            }
        }
    } while(ACKSYNFIN(chunk_buf, totalBytesChunk, SYNMessage, true));
    //check for ACK/SYN
}
void createConnection() {
    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    int totalBytesChunk = CONTROLBITLENGTH + PAYLOADSIZE;

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    timeout.tv_usec = 40000; // and microseconds
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }

    //SYN
    SYNFIN(chunk_buf, totalBytesChunk, true);

    //ACK/SYN
    ACKSYNFIN(chunk_buf, totalBytesChunk, true, false);

    //ACK
    ACK(chunk_buf, totalBytesChunk, true);

    // Reset timeout (set to 0 for a non-blocking call)
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }
}

/*
Send FIN packet
Wait for FIN/ACK packet for 1 second else go back to previous step 
Send ACK packet 
Wait for FIN/ACK message for 1 second and if none is recieved continue  
*/

void closeConnection() {
    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    int totalBytesChunk = CONTROLBITLENGTH + PAYLOADSIZE;

    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    timeout.tv_usec = 40000; // and microseconds
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }

    //FIN
    SYNFIN(chunk_buf, totalBytesChunk, false);

    //ACK/FIN
    ACKSYNFIN(chunk_buf, totalBytesChunk, false, false);

    //ACK
    ACK(chunk_buf, totalBytesChunk, false);

    // Reset timeout (set to 0 for a non-blocking call)
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }
}


void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    char* buffer = NULL;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }

	/* Determine how many bytes to transfer */
    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    createConnection();
    
    // read bytesToTransfer bytes from file to buffer
    buffer = calloc(1, MAXDATASIZE);
    fseek(fp, 0L, SEEK_END);
    unsigned long long int sizeOfFile = ftell(fp);
    rewind(fp);
    if(bytesToTransfer > sizeOfFile) {
        bytesToTransfer = sizeOfFile;
    }

    // read file into buffer
    size_t bytesRead = fread(buffer, 1, bytesToTransfer, fp);
    if (bytesRead != bytesToTransfer) {
        perror("Failed to read the specified number of bytes");
        free(buffer);
        fclose(fp);
        exit(1);
    }

    // send data from buffer in chunks via sendto()
    size_t total_bytes_sent = 0;
    // total to send = buflen + (numchunks that we will send) * (control bits per chunk)
    size_t total_bytes_to_send = strlen(buffer) + _ceil(( (double) strlen(buffer) ) / PAYLOADSIZE)*CONTROLBITLENGTH;
    //Keep track of number of data bits read
    size_t total_bytes_read_from_buffer = 0;

    //Seperate buffer for control bits like sequence number, startup, and close
    char control_buf[CONTROLBITLENGTH];
    // CONTROLBITLENGTH is the num bits we have in each chunk to enforce TCP
    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE] = '\0';
    int sequenceNumber = 0;

    // populate chunk buf from buffer
    while (total_bytes_sent < total_bytes_to_send) {
        memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);

        // populate chunk_buf with control bits and payload
        memset(control_buf, '\0', CONTROLBITLENGTH);
        sprintf(control_buf, "%d", sequenceNumber);
        memcpy(chunk_buf, control_buf, CONTROLBITLENGTH);
        // last chunk will not require full PAYLOADSIZE to send, so copy bytes as necessary.
        size_t bytes_of_buff_to_send = min(PAYLOADSIZE, strlen(buffer) - total_bytes_read_from_buffer);
        memcpy(&chunk_buf[CONTROLBITLENGTH], &buffer[total_bytes_read_from_buffer], bytes_of_buff_to_send);

        // send chunk, and make sure that full chunk is sent via while loop.
        size_t ctr = 0;
        size_t chunk_bytes_sent = 0;
        size_t bytes_to_send_for_chunk = PAYLOADSIZE + CONTROLBITLENGTH;
        while(chunk_bytes_sent != bytes_to_send_for_chunk) {
            if((chunk_bytes_sent += sendto(s, &chunk_buf[chunk_bytes_sent], bytes_to_send_for_chunk-chunk_bytes_sent, 0, (struct sockaddr *) &si_other, slen)) == -1) {
                    perror("chunk sending failure.\n");
                    exit(1);
            }
            if (ctr > 1) {
                printf("retrying send, not all bytes in chunk were sent...\n");
            }

            ++ctr;
        }
        
        //keep track of number of bytes sent and number of bytes read
        total_bytes_read_from_buffer += bytes_of_buff_to_send;
        total_bytes_sent += chunk_bytes_sent;
        sequenceNumber++;
    }

    //send final messages to close the connection
    closeConnection();

    printf("Closing the socket\n");
    free(buffer);
    close(s);
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);

    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}