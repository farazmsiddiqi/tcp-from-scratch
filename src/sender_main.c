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
#include <sys/time.h>

#define MAXDATASIZE 500000
#define PAYLOADSIZE 1000
#define CONTROLBITLENGTH 20

struct sockaddr_in si_other;
int s, slen;

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
    size_t total_bytes_read_from_buffer = 0;

    char control_buf[CONTROLBITLENGTH];
    // CHUNKSIZE+1 is the payload and null terminator
    // CONTROLBITLENGTH is the num bits we have in each chunk to enforce selective ACK protocol.
    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE] = '\0';

    // populate chunk buf from buffer
    while (total_bytes_sent != total_bytes_to_send) {
        memset(chunk_buf, 0, CONTROLBITLENGTH + PAYLOADSIZE);

        // populate chunk_buf with control bits and payload
        memset(control_buf, 0, CONTROLBITLENGTH);
        sprintf(control_buf, "%ld", strlen(buffer));
        memcpy(chunk_buf, control_buf, CONTROLBITLENGTH);
        // last chunk will not require full PAYLOADSIZE to send.
        size_t bytes_of_buff_to_send = min(PAYLOADSIZE, strlen(buffer) - total_bytes_read_from_buffer);
        memcpy(&chunk_buf[CONTROLBITLENGTH], &buffer[total_bytes_read_from_buffer], bytes_of_buff_to_send);

        // send chunk, and make sure that full chunk is sent via while loop.
        size_t ctr = 0;
        size_t chunk_bytes_sent = 0;
        size_t bytes_to_send_for_chunk = bytes_of_buff_to_send + CONTROLBITLENGTH;
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
        
        total_bytes_read_from_buffer += bytes_of_buff_to_send;
        total_bytes_sent += chunk_bytes_sent;
    }

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