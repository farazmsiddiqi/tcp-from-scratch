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
#include <time.h>

#define MAXDATASIZE 1000000
#define PAYLOADSIZE 500
#define CONTROLBITLENGTH 12
#define SLOWSTARTTHRESHOLD 100
#define DUPACKTHRESHOLD 3
//in mileseconds
#define TIMERTIMEOUT 100

// TCP struct
typedef struct {
    size_t seq_num;
    char* payload_start;
    int timer_timestamp_msec;
    bool sent_packet;
} tcp_struct;

//TCP state enum
enum tcp_state {
    slow_start = 0,
    fast_recovery = 1,
    congestion_avoidance = 2
};

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
    v->size = new_capacity;
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

// TCP vars
size_t SST = SLOWSTARTTHRESHOLD;
size_t current_timeout = TIMERTIMEOUT;
size_t timeout_threadshold = TIMERTIMEOUT; //adjust based on new algo tbd
size_t hack = 0;
size_t seqNum = 1;
size_t dupAckCounter = 0;
double CW = 1;
vec packet_arr;
clock_t timer;
enum tcp_state currentState = slow_start;

// socket global vars
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

int _floor(double num) {
    int integerPart = (int)num;
    return integerPart;
}

int min(int a, int b) {
    return (a < b) ? a : b;
}

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
            perror("chunk sending failure in phase 1.\n");
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
                    perror("chunk sending failure in phase 2.\n");
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
                perror("chunk sending failure in phase 3.\n");
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

void sendPackets(char *chunk_buf, char *control_buf, char *file_buf, size_t * total_bytes_read_from_buffer, size_t * total_bytes_sent) {

    printf("sending packets\n");

    for(size_t i = 0; i < _floor(CW); i++) {
        memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
        memset(control_buf, '\0', CONTROLBITLENGTH);
        tcp_struct * current_packet = get(&packet_arr, i);
        printf("sending packet %ld \n", current_packet->seq_num);
        if(!current_packet->sent_packet) {
            // populate chunk_buf with control bits and payload
            sprintf(control_buf, "SEQ %ld", current_packet->seq_num);
            memcpy(chunk_buf, control_buf, CONTROLBITLENGTH);
            size_t bytes_of_buff_to_send = PAYLOADSIZE;
            memcpy(&chunk_buf[CONTROLBITLENGTH], current_packet->payload_start, bytes_of_buff_to_send);

            // send chunk, and make sure that full chunk is sent via while loop.
            size_t chunk_bytes_sent = 0;
            size_t bytes_to_send_for_chunk = PAYLOADSIZE + CONTROLBITLENGTH;
            while(chunk_bytes_sent != bytes_to_send_for_chunk) {
                if((chunk_bytes_sent += sendto(s, &chunk_buf[chunk_bytes_sent], bytes_to_send_for_chunk-chunk_bytes_sent, 0, (struct sockaddr *) &si_other, slen)) == -1) {
                        perror("chunk sending failure.\n");
                        exit(1);
                }
            }
            
            //keep track of number of bytes sent and number of bytes read
            *total_bytes_read_from_buffer += bytes_of_buff_to_send;
            *total_bytes_sent += chunk_bytes_sent;
            current_packet->sent_packet = true;
            current_packet->timer_timestamp_msec = (clock()- timer) * 1000 /CLOCKS_PER_SEC;
        }
    }     
}

void checkTimer() {
    //mark time that has passed so far
    int elapsedTime = (clock() - timer) * 1000 / CLOCKS_PER_SEC;
    //check if a timeout has occured
    if(elapsedTime > current_timeout) {
        printf("sender: timeout occured \n");
        //reset window
        SST = (int)CW/2;
        dupAckCounter = 0;
        for(size_t i = 1; i < _floor(CW); i++) {
            erase(&packet_arr, packet_arr.size-1);
        }
        CW = 1;
        tcp_struct * firstPacket = get(&packet_arr,0);
        firstPacket->timer_timestamp_msec = 0;
        firstPacket->sent_packet = false;
        currentState = slow_start;
        current_timeout = timeout_threadshold;
        timer = clock();
    }
}

void slowStart(char *control_buf, char *file_buf, size_t ACKSequenceNumber) {
    //handle ACK 
    if(ACKSequenceNumber > hack) {
        int slideDistance = 0;
        int tCurrent = 0; 
        int tNext = 0;
        //remove acked packet
        while(get(&packet_arr, slideDistance)->seq_num != ACKSequenceNumber) {
            slideDistance++;
        }
        tCurrent = get(&packet_arr, slideDistance)->timer_timestamp_msec;
        if(slideDistance+1 == _floor(CW)) {
            free_vector(&packet_arr);
        }
        else {
            while(slideDistance >= 0) {
                erase(&packet_arr, 0);
                slideDistance--;
            }
            tNext = get(&packet_arr, 0)->timer_timestamp_msec;
        }

        //slide and expand window for new packets
        CW = CW + (ACKSequenceNumber - hack);
        while(packet_arr.size != _floor(CW)) {
            seqNum++;
            tcp_struct newEntry = {seqNum, file_buf + seqNum*PAYLOADSIZE, 0, false};
            push_back(&packet_arr,newEntry);
        }
        
        //Adjust timeout based on single timer equation
        current_timeout = (tCurrent + timeout_threadshold - ((clock() - timer) * 1000 / CLOCKS_PER_SEC)) + (tNext - tCurrent);
        timer = clock();
        dupAckCounter = 0;
        hack = ACKSequenceNumber;
    }
    else if(ACKSequenceNumber == hack) {
        dupAckCounter++;
    }
}

void recievePacket(char *control_buf, char *file_buf) {
    //get packet
    int currentBytesRecieved = 0;
    int totalBytesACKChunk = CONTROLBITLENGTH;
    memset(control_buf, '\0', CONTROLBITLENGTH);
    printf("reciving packet \n");

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    //timeout.tv_usec = timeout_threadshold - (timer) * 1000 / CLOCKS_PER_SEC; // and miliseconds
    timeout.tv_usec = timeout_threadshold;
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }

    while(currentBytesRecieved != totalBytesACKChunk) {
        if((currentBytesRecieved += recvfrom(s, &control_buf[currentBytesRecieved], totalBytesACKChunk -currentBytesRecieved, 0, &addr, &fromlen)) == -1) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                    // handle timeout
                    printf("recvfrom() timed out\n");
                    break;
                } else {
                    // handle other errors
                    perror("chunk sending failure in phase 2.\n");
                    exit(1);
                }
        }
    }

    // Reset timeout (set to 0 for a non-blocking call)
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        diep("setsockopt");
    }
    
    size_t ACKSequenceNumber = 0;
    int success = sscanf(control_buf, "ACK %ld", &ACKSequenceNumber);
    if(success != 1) {
        printf("ACK not recieved\n");
        return;
    }

    if(currentState == slow_start) {
        slowStart(control_buf, file_buf, ACKSequenceNumber);
    }
}


void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    char* file_buf = NULL;
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
    printf("socket created\n");

    createConnection();
    
    // read bytesToTransfer bytes from file to buffer
    file_buf = calloc(1, MAXDATASIZE);
    fseek(fp, 0L, SEEK_END);
    unsigned long long int sizeOfFile = ftell(fp);
    rewind(fp);
    if(bytesToTransfer > sizeOfFile) {
        bytesToTransfer = sizeOfFile;
    }

    // read file into buffer
    size_t bytesRead = fread(file_buf, 1, bytesToTransfer, fp);
    if (bytesRead != bytesToTransfer) {
        perror("Failed to read the specified number of bytes");
        free(file_buf);
        fclose(fp);
        exit(1);
    }

    // send data from buffer in chunks via sendto()
    size_t total_bytes_sent = 0;
    // total to send = buflen + (numchunks that we will send) * (control bits per chunk)
    size_t total_bytes_to_send = strlen(file_buf) + _ceil(( (double) strlen(file_buf) ) / PAYLOADSIZE)*CONTROLBITLENGTH;
    //Keep track of number of data bits read
    size_t total_bytes_read_from_buffer = 0;

    //Seperate buffer for control bits like sequence number, startup, and close
    char control_buf[CONTROLBITLENGTH];
    // CONTROLBITLENGTH is the num bits we have in each chunk to enforce TCP
    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE] = '\0';
    //Start timer
    timer = clock();
    //Load first packet to be sent
    tcp_struct firstPacket = {seqNum, file_buf, 0, false};
    push_back(&packet_arr, firstPacket);

    // populate chunk buf from buffer
    while (total_bytes_sent < total_bytes_to_send) {
        //check timeout
        checkTimer();

        //send packets
        sendPackets(chunk_buf, control_buf, file_buf, &total_bytes_read_from_buffer, &total_bytes_sent);

        //recieve packets
        recievePacket(control_buf, file_buf);
    }

    //send final messages to close the connection
    closeConnection();

    printf("Closing the socket\n");
    free(file_buf);
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