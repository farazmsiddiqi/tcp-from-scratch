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

#define MAXDATASIZE 500000

struct sockaddr_in si_me, si_other;
int s, slen;

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
    size_t numBytesRecieved = 0;
    struct sockaddr addr;
    socklen_t fromlen = sizeof(addr);

    if((numBytesRecieved = recvfrom(s, buffer, MAXDATASIZE-1, 0, &addr, &fromlen)) == -1) {
        free(buffer);
        exit(1);
    }

    if(numBytesRecieved != write_to_file(buffer, destinationFile)) {
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
