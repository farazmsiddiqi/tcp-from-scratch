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
#include <vector>
#include <string>

#define PAYLOADSIZE 500
#define CONTROLBITLENGTH 12

struct sockaddr_in si_me, si_other;
int s, slen;
struct sockaddr addr;
socklen_t fromlen = sizeof(addr);

// TCP struct
struct tcp_struct{
    size_t seq_num;
    char* payload_chunk;
    bool packet_present;
};

//TCP data structures
size_t highestInOrderPacketSequenceNumber = 0;
std::vector<tcp_struct> OOO_packet_arr;
size_t last_packet_size = 0;
size_t total_number_packets_waiting = 0;

std::string socketError1;
void diep(std::string &socketError1) {
    perror(socketError1.c_str());
    exit(1);
}

//append results to file
int write_to_file(char *buf, char *fname) {
    FILE *fp;
    int bytes_written;
    size_t buf_len = PAYLOADSIZE;
    //printf("number of packets waiting %ld and size of last packet %ld \n", total_number_packets_waiting, last_packet_size);
    if(total_number_packets_waiting == 0) {
        buf_len = last_packet_size;
    }
    
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
    memset(chunk_buf, '\0', CONTROLBITLENGTH);
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
    memset(chunk_buf, '\0', CONTROLBITLENGTH);
    bool ACKRecieved = false;

    while(!ACKRecieved) {
        int currentBytesRecieved = 0;
        while(currentBytesRecieved != totalBytesChunk) {
            if ((currentBytesRecieved += recvfrom(s, &chunk_buf[currentBytesRecieved], totalBytesChunk-currentBytesRecieved, 0, &addr, &fromlen)) == -1) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                    // handle timeout
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
    int totalBytesChunk = CONTROLBITLENGTH;

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    timeout.tv_usec = 10000; // and microseconds
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        socketError1 = "setsockopt";
        diep(socketError1);
    }

    //TCP
    // printf("SYN\n");
    SYNFINACK(chunk_buf, totalBytesChunk, true);
    // printf("ACK\n");
    ACK(chunk_buf, totalBytesChunk, true);

    // Reset timeout (set to 0 for a non-blocking call)
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        socketError1 = "setsockopt";
        diep(socketError1);
    }
}

void closeConnection(char *chunk_buf) {
    int totalBytesChunk = CONTROLBITLENGTH;

     // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    timeout.tv_usec = 10000; // and microseconds
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        socketError1 = "setsockopt";
        diep(socketError1);
    }

    //TCP 
    SYNFINACK(chunk_buf, totalBytesChunk, false);

    ACK(chunk_buf, totalBytesChunk, false);

    // Reset timeout (set to 0 for a non-blocking call)
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        socketError1 = "setsockopt";
        diep(socketError1);
    }
}

bool parseMessage(char *chunk_buf, bool * connectionClose, char* destinationFile) {
    if(strstr(chunk_buf, "SYN") != NULL) {
        // printf("control packet\n");
        sscanf(chunk_buf, "SYN %ld %ld", &total_number_packets_waiting, &last_packet_size);
        if(last_packet_size == 0) {
            last_packet_size = PAYLOADSIZE;
        }
        openConnection(chunk_buf);
        //control packet was found
        return false;
    }
    else if(strstr(chunk_buf, "FIN") != NULL) {
        closeConnection(chunk_buf);
        *connectionClose = true;
        return false;
    }
    else {
        size_t PacketSequenceNumber = 0;
        int success = sscanf(chunk_buf, "SEQ %ld", &PacketSequenceNumber);
        if(success != 1) {
            return true;
        }
        // printf("data packet %ld \n", PacketSequenceNumber);

        //Case 1 next packet recieved 
        if(PacketSequenceNumber == highestInOrderPacketSequenceNumber+1) {
            // printf("Case 1 next sequence number\n");
            // printf("Resize packet array\n");
            // Step 1 Place in OOO buffer if needed
            if(OOO_packet_arr.size() == 0) {
                OOO_packet_arr.resize(1);
            }
            tcp_struct & currentEntry = OOO_packet_arr[0];
            currentEntry.seq_num = PacketSequenceNumber;
            currentEntry.packet_present = true;
            currentEntry.payload_chunk = new char[PAYLOADSIZE+1];
            memcpy(currentEntry.payload_chunk, &chunk_buf[CONTROLBITLENGTH], PAYLOADSIZE);

            // printf("Write to file\n");
            //Step 2 Write all available chunks to file
            int packetsWritten = 0;
            for(size_t i = 0; i < OOO_packet_arr.size() && OOO_packet_arr[i].packet_present; i++) {
                packetsWritten++;
                total_number_packets_waiting--;
                write_to_file(OOO_packet_arr[i].payload_chunk, destinationFile);
                highestInOrderPacketSequenceNumber++;
            }

            // printf("clear buffer\n");
            //Step 3 Clear all written packets from buffer
            while(packetsWritten != 0) {
                // printf("packet array begin %d, memory address %p\n", packetsWritten, OOO_packet_arr[0].payload_chunk);
                delete [] OOO_packet_arr[0].payload_chunk;
                OOO_packet_arr.erase(OOO_packet_arr.begin());
                packetsWritten--;
            } 
            return true;
        } 
        else if(PacketSequenceNumber > highestInOrderPacketSequenceNumber+1) {
            //Case 2 OOO packet recieved 
            //OOO buffer base highestInOrderPacketSequenceNumber+1 and tail PacketSequence
            //Step 1 expand OOO buffer if needed
            // printf("Case 2 OOO packet \n");
            if(OOO_packet_arr.size() == 0) {
                //Set locations for future packets
                // printf("Case 2 resize empty packet array\n");
                OOO_packet_arr.resize(PacketSequenceNumber - highestInOrderPacketSequenceNumber);
                int sequenceNumberPopulation = highestInOrderPacketSequenceNumber+1;
                for(size_t i = 0; i < OOO_packet_arr.size(); i++) {
                    tcp_struct & currentEntry = OOO_packet_arr[i];
                    currentEntry.seq_num = sequenceNumberPopulation;
                    currentEntry.payload_chunk = nullptr;
                    currentEntry.packet_present = false;
                    sequenceNumberPopulation++;
                }
            }
            else if(OOO_packet_arr.back().seq_num < PacketSequenceNumber) {
                //Expand tail of buffer for future packets
                // printf("Case 2 expand packet array\n");
                size_t i = OOO_packet_arr.back().seq_num;
                for(; i <= PacketSequenceNumber; i++) {
                    tcp_struct nextEntry;
                    nextEntry.seq_num = i;
                    nextEntry.payload_chunk = nullptr;
                    nextEntry.packet_present = false;
                    OOO_packet_arr.push_back(nextEntry);

                }
            }

            //Step 2 store OOO packet
            // printf("Store OOO packet in array size %ld, packet seq %ld and highest packet seen+1 %ld \n", OOO_packet_arr.size(),PacketSequenceNumber, highestInOrderPacketSequenceNumber+1);
            tcp_struct &entry = OOO_packet_arr[PacketSequenceNumber-(highestInOrderPacketSequenceNumber+1)];
            entry.packet_present = true;
            entry.payload_chunk = new char[PAYLOADSIZE+1];
            memcpy(entry.payload_chunk, &chunk_buf[CONTROLBITLENGTH], PAYLOADSIZE);
            //data packet was found
            return true;
        }
        return true;
    }
}

bool recievePacket(char *chunk_buf, bool * closeConnection, char* destinationFile) {

    //Number of bytes recieved in a single recv call
    size_t num_bytes_response_chunk = 0; 
    //Number of bytes recieved in all recv calls for a single chunk 
    size_t num_bytes_recieved_chunk = 0;
    //Number of bytes expected in each packet
    size_t num_bytes_expected_chunk = CONTROLBITLENGTH + PAYLOADSIZE;  
    //read bytes unto chunk safely 
    memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
    while(num_bytes_recieved_chunk != num_bytes_expected_chunk) {
        // // printf("reciving packet waiting on recvfrom\n");
        if ((num_bytes_response_chunk = recvfrom(s, &chunk_buf[num_bytes_recieved_chunk], num_bytes_expected_chunk-num_bytes_recieved_chunk, 0, &addr, &fromlen)) == -1) {
            perror("recvfrom returned -1");
            exit(1);
        }
        num_bytes_recieved_chunk += num_bytes_response_chunk;
    }

    //Extract data and control bits and write to file 
    if(parseMessage(chunk_buf, closeConnection, destinationFile)) {
        //distiguish between data (true) and control (false)
        return true;
    }
    else {
        return false;
    }
}

void sendPacket(char * control_buf, bool * closeConnection) {
    if(*closeConnection) {
        return;
    }

    //send small ACK for highest sequence number recieved
    int currentBytesSent = 0;
    int totalBytesControlChunk = CONTROLBITLENGTH;
    memset(control_buf, '\0', CONTROLBITLENGTH);
    sprintf(control_buf, "ACK %ld", highestInOrderPacketSequenceNumber);
    // printf("Sending ACK for packet %ld\n", highestInOrderPacketSequenceNumber);
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


    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        socketError1 = "socket";
        diep(socketError1);
    }

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    // printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1) {
        socketError1 = "bind";
        diep(socketError1);
    }

	/* Now receive data and send acknowledgements */   
    FILE *fp = fopen(destinationFile, "wb");
    fclose(fp);
    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    char control_buf[CONTROLBITLENGTH+1];
    bool closeConnection = false;
    //Continue untill connection close process is done
    while(!closeConnection) {
        //recieve packet
        // printf("recieve packet start \n");
        bool sendACK = recievePacket(chunk_buf, &closeConnection, destinationFile);

        //send ACK if needed packet
        if(sendACK) {
            // printf("send packet start \n");
            sendPacket(control_buf, &closeConnection);
        }
    }

    close(s);
	// printf("%s received.", destinationFile);
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
