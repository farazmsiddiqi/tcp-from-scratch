/* 
 * File:   receiver_main.c
 * Author: 
 *
 * Created on
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
#include <stdbool.h>

#define MAXDATASIZE 500000
#define PAYLOADSIZE 500
#define CONTROLBITLENGTH 20

struct sockaddr_in si_me, si_other;
int s, slen;
struct sockaddr addr;
socklen_t fromlen = sizeof(addr);

void diep(char *s) {
    perror(s);
    exit(1);
}

int write_to_file(char *buf, char *fname) {
    FILE *fp;
    int bytes_written;
    size_t buf_len = strlen(buf);

    fp = fopen(fname, "wb");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }

    bytes_written = fwrite(buf, sizeof(char), buf_len, fp);
    fclose(fp);

    return bytes_written;
}

/*
Recieve SYN packet
Send SYN/ACK
Wait for ACK else re-send SYN/ACK

Recieve FIN packet
Send FIN/ACK
Wait for ACK else re-send FIN/ACK
*/

void openConnection(char *chunk_buf) {
    int totalBytesChunk = CONTROLBITLENGTH + PAYLOADSIZE;
    int currentBytesSent = 0;
    int currentBytesRecieved = 0;

    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    sprintf(chunk_buf, "%s", "SYN/ACK");
    while(currentBytesSent != totalBytesChunk) {
        if ((currentBytesSent += sendto(s, &chunk_buf[currentBytesSent], totalBytesChunk-currentBytesSent, 0, &addr, fromlen)) == -1) {
            perror("recvfrom returned -1");
            free(chunk_buf);
            exit(1);
        }
    }


    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    bool ACKRecieved = false;
    while(!ACKRecieved) {
        while(currentBytesRecieved != totalBytesChunk) {
            if ((currentBytesRecieved += recvfrom(s, &chunk_buf[currentBytesRecieved], totalBytesChunk-currentBytesRecieved, 0, &addr, &fromlen)) == -1) {
                perror("recvfrom returned -1");
                free(chunk_buf);
                exit(1);
            }
        }
        if(strstr(chunk_buf, "ACK") != NULL) {
            ACKRecieved = true;
        }
    }
}

void closeConnection(char *chunk_buf) {
    int totalBytesChunk = CONTROLBITLENGTH + PAYLOADSIZE;
    int currentBytesSent = 0;
    int currentBytesRecieved = 0;

    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    sprintf(chunk_buf, "%s", "FIN/ACK");
    while(currentBytesSent != totalBytesChunk) {
        if ((currentBytesSent += sendto(s, &chunk_buf[currentBytesSent], totalBytesChunk-currentBytesSent, 0, &addr, fromlen)) == -1) {
            perror("recvfrom returned -1");
            free(chunk_buf);
            exit(1);
        }
    }


    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    bool ACKRecieved = false;
    while(!ACKRecieved) {
        while(currentBytesRecieved != totalBytesChunk) {
            if ((currentBytesRecieved += recvfrom(s, &chunk_buf[currentBytesRecieved], totalBytesChunk-currentBytesRecieved, 0, &addr, &fromlen)) == -1) {
                perror("recvfrom returned -1");
                free(chunk_buf);
                exit(1);
            }
        }
        if(strstr(chunk_buf, "ACK") != NULL) {
            ACKRecieved = true;
        }
    }
}

bool parseMessage(char *chunk_buf, char *data_buf, int num_chunks_recieved_total, bool * connectionClose) {
    if(strstr(chunk_buf, "SYN") != NULL) {
        openConnection(chunk_buf);
        return true;
    }
    else if(strstr(chunk_buf, "FIN") != NULL) {
        closeConnection(chunk_buf);
        *connectionClose = true;
        return true;
    }
    else {
        memcpy(&data_buf[num_chunks_recieved_total*PAYLOADSIZE], &chunk_buf[CONTROLBITLENGTH], PAYLOADSIZE);
        chunk_buf[CONTROLBITLENGTH] = '\0';
        return false;
    }
}


void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    
    slen = sizeof (si_other);


    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");


	/* Now receive data and send acknowledgements */   
    char* buffer = calloc(1, MAXDATASIZE); 
    //Number of bytes recieved in a single recv call
    size_t num_bytes_response_chunk = 0; 
    //Number of bytes expected in each packet
    size_t num_bytes_expected_chunk = CONTROLBITLENGTH + PAYLOADSIZE;  
    //Number of bytes recieved in all recv calls for a single check 
    size_t num_bytes_recieved_chunk = 0;
    //Number of bytes recieved in the whole connection 
    size_t num_bytes_recieved_total = 0;
    //Number of chunks recieved in the whole connection
    size_t num_chunks_recieved_total = 0;

    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    bool closeConnection = false;
    //Continue untill connection close process is done
    while(!closeConnection) {
        //read bytes unto chunk safely 
        memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
        while(num_bytes_recieved_chunk != num_bytes_expected_chunk) {
            if ((num_bytes_response_chunk = recvfrom(s, &chunk_buf[num_bytes_recieved_chunk], num_bytes_expected_chunk-num_bytes_recieved_chunk, 0, &addr, &fromlen)) == -1) {
                perror("recvfrom returned -1");
                free(buffer);
                exit(1);
            }
            num_bytes_recieved_chunk += num_bytes_response_chunk;
        }

        //Extract data and control bits and write to main data buffer
        if(!parseMessage(chunk_buf, buffer, num_chunks_recieved_total,&closeConnection)) {
            //distiguish between data and control 
            num_chunks_recieved_total += 1;
            num_bytes_recieved_total += num_bytes_expected_chunk;
        }
        //Reset for next iteration
        num_bytes_response_chunk = 0;
        num_bytes_recieved_chunk = 0;
    }

    //Write to file
    if(num_chunks_recieved_total*PAYLOADSIZE <= write_to_file(buffer, destinationFile)) {
        perror("file len != buffer len");
        free(buffer);
        exit(1);
    }

    close(s);
	printf("%s received.", destinationFile);
    return;
}


int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}