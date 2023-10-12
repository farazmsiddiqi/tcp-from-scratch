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
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>

#define MAXDATASIZE 1000000
#define PAYLOADSIZE 500
#define CONTROLBITLENGTH 12

struct sockaddr_in si_me, si_other;
int s, slen;
struct sockaddr addr;
socklen_t fromlen = sizeof(addr);

// TCP struct
typedef struct {
    size_t seq_num;
    char* payload_chunk;
} tcp_struct;

// VECTOR IMPLEMENTATION
typedef struct {
    tcp_struct* data;
    size_t size;
    size_t capacity;
} vec;

void init_vector(vec* v, size_t initial_capacity) {
    v->data = (tcp_struct*)malloc(initial_capacity * sizeof(tcp_struct));
    v->size = 0;
    v->capacity = initial_capacity;
}

void free_vector(vec* v) {
    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

void resize_vector(vec* v, size_t new_capacity) {
    tcp_struct* new_data = (tcp_struct*)malloc(new_capacity * sizeof(tcp_struct));
    memcpy(new_data, v->data, v->size * sizeof(tcp_struct));
    free(v->data);
    v->data = new_data;
    v->capacity = new_capacity;
}

void push_back(vec* v, tcp_struct value) {
    if (v->size == v->capacity) {
        resize_vector(v, v->capacity * 2);
    }
    v->data[v->size++] = value;
}

tcp_struct* get(vec* v, size_t index) {
    if (index < v->size) {
        return &v->data[index];
    }

    return NULL;
}

void erase(vec* v, size_t index) {
    if (index < v->size) {
        memmove(&v->data[index], &v->data[index + 1], (v->size - index - 1) * sizeof(tcp_struct));
        v->size--;
    }
}
// END VECTOR IMPLEMENTATION

//TCP data structures
size_t highestInOrderPacketSequenceNumber = 0;
vec OOO_packet_arr;

void diep(char *s) {
    perror(s);
    exit(1);
}

//append results to file
int write_to_file(char *buf, char *fname) {
    FILE *fp;
    int bytes_written;
    size_t buf_len = strlen(buf);

    fp = fopen(fname, "ab");
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
Wait for ACK for 40 millesecond else re-send SYN/ACK

Recieve FIN packet
Send FIN/ACK
Wait for ACK for 40 millesecond else re-send FIN/ACK
*/

void SYNFINACK(char * chunk_buf, int totalBytesChunk, bool SYNMessage) {
    int currentBytesSent = 0;
    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    if(SYNMessage) {
        sprintf(chunk_buf, "%s", "SYN/ACK");
    }
    else {
        sprintf(chunk_buf, "%s", "FIN/ACK");
    }

    while(currentBytesSent != totalBytesChunk) {
        if ((currentBytesSent += sendto(s, &chunk_buf[currentBytesSent], totalBytesChunk-currentBytesSent, 0, &addr, fromlen)) == -1) {
            perror("recvfrom returned -1");
            free(chunk_buf);
            exit(1);
        }
    }
}

void ACK(char * chunk_buf, int totalBytesChunk, bool SYNMessage) {
    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    bool ACKRecieved = false;

    while(!ACKRecieved) {
        int currentBytesRecieved = 0;
        while(currentBytesRecieved != totalBytesChunk) {
            if ((currentBytesRecieved += recvfrom(s, &chunk_buf[currentBytesRecieved], totalBytesChunk-currentBytesRecieved, 0, &addr, &fromlen)) == -1) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                    // handle timeout
                    printf("recvfrom() timed out\n");
                    ACKRecieved = false;
                    break;
                } else {
                    // handle other errors
                    perror("chunk sending failure.\n");
                    exit(1);
                }
            }
        }
        if(strstr(chunk_buf, "ACK") != NULL) {
            ACKRecieved = true;
        }
        else {
            SYNFINACK(chunk_buf, totalBytesChunk, SYNMessage);
        }
    }
}

void openConnection(char *chunk_buf) {
    int totalBytesChunk = CONTROLBITLENGTH + PAYLOADSIZE;

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    timeout.tv_usec = 40000; // and microseconds
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }

    //TCP
    SYNFINACK(chunk_buf, totalBytesChunk, true);

    ACK(chunk_buf, totalBytesChunk, true);

    // Reset timeout (set to 0 for a non-blocking call)
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }
}

void closeConnection(char *chunk_buf) {
    int totalBytesChunk = CONTROLBITLENGTH + PAYLOADSIZE;

     // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    timeout.tv_usec = 40000; // and microseconds
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }

    //TCP 
    SYNFINACK(chunk_buf, totalBytesChunk, false);

    ACK(chunk_buf, totalBytesChunk, false);

    // Reset timeout (set to 0 for a non-blocking call)
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
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
        size_t PacketSequenceNumber = 0;
        int success = sscanf(chunk_buf, "SEQ %ld", PacketSequenceNumber);
        if(success != 1) {
            return;
        }
        //TODO: get rid of data buf and append to file
        //Implement algorithmns of reciever logic
        memcpy(&data_buf[num_chunks_recieved_total*PAYLOADSIZE], &chunk_buf[CONTROLBITLENGTH], PAYLOADSIZE);
        
        return false;
    }
}

void recievePacket(char *chunk_buf, char * buffer, size_t * num_chunks_recieved_total, size_t *num_bytes_recieved_total, bool * closeConnection) {

    //Number of bytes recieved in a single recv call
    size_t num_bytes_response_chunk = 0; 
    //Number of bytes recieved in all recv calls for a single chunk 
    size_t num_bytes_recieved_chunk = 0;
    //Number of bytes expected in each packet
    size_t num_bytes_expected_chunk = CONTROLBITLENGTH + PAYLOADSIZE;  
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

    //Extract data and control bits and write to file 
    if(!parseMessage(chunk_buf, buffer, *num_chunks_recieved_total, closeConnection)) {
        //distiguish between data and control 
        *num_chunks_recieved_total += 1;
        *num_bytes_recieved_total += num_bytes_expected_chunk;
    }
}

void sendPacket(char * control_buf, bool * closeConnection) {
    if(*closeConnection == true) {
        return;
    }
    //send small ACK for highest sequence number recieved
    int currentBytesSent = 0;
    int totalBytesControlChunk = CONTROLBITLENGTH;
    memset(control_buf, '\0', CONTROLBITLENGTH);
    sprintf(control_buf, "ACK %ld", highestInOrderPacketSequenceNumber);
    //send all bytes
    while(currentBytesSent != totalBytesControlChunk) {
    if ((currentBytesSent += sendto(s, &control_buf[currentBytesSent], totalBytesControlChunk-currentBytesSent, 0, &addr, fromlen)) == -1) {
        perror("recvfrom returned -1");
        free(control_buf);
        exit(1);
    }
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
    //Number of bytes recieved in the whole connection 
    size_t num_bytes_recieved_total = 0;
    //Number of chunks recieved in the whole connection
    size_t num_chunks_recieved_total = 0;

    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    char control_buf[CONTROLBITLENGTH+1];
    bool closeConnection = false;
    //Continue untill connection close process is done
    while(!closeConnection) {
        //recieve packet
        recievePacket(chunk_buf, buffer, &num_chunks_recieved_total, &num_bytes_recieved_total, &closeConnection);

        //send packet
        sendPacket(control_buf, &closeConnection);

    }

    //Write to file (change do for every chunk)
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