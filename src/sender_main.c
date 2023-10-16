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
#include <vector>
#include <string>

#define MAXDATASIZE 30000000
#define PAYLOADSIZE 500
#define CONTROLBITLENGTH 12
#define SLOWSTARTTHRESHOLD 100
#define DUPACKTHRESHOLD 3
//in milliseconds
#define TIMERTIMEOUT_MSEC 100
// Convert milliseconds to seconds
#define MSEC_TO_SEC(msec) ((msec) / 1000)
// Convert milliseconds to nanoseconds
#define MSEC_TO_NSEC(ms) ((ms) * 1000000)
// Convert nanoseconds to milliseconds
#define NSEC_TO_MSEC(ns) ((ns) / 1000000)
// Convert microseconds to milliseconds
#define USEC_TO_MSEC(us) ((us) / 1000)
// Convert milliseconds to microseconds
#define MSEC_TO_USEC(ms) ((ms) * 1000)

// TCP struct
struct tcp_struct {
    size_t seq_num;
    char* payload_start;
    int timer_timestamp_msec;
    bool sent_packet;
};

//TCP state enum
enum tcp_state {
    slow_start = 0,
    fast_recovery = 1,
    congestion_avoidance = 2
};


// TCP vars
pthread_mutex_t timer_lock;
bool timeout_occurred = false;
timer_t tcp_timeout_timer;
size_t new_timeout = TIMERTIMEOUT_MSEC;
clock_t timer;
size_t SST = SLOWSTARTTHRESHOLD;
size_t hack = 0;
size_t seqNum = 1;
size_t numPacketsCompleted = 0;
size_t dupAckCounter = 0;
size_t total_packets_to_send = 0;
size_t last_packet_size = 0;
double CW = 1;
std::vector<tcp_struct> packet_arr;
enum tcp_state currentState = slow_start;

// socket global vars
struct sockaddr_in si_other;
int s, slen;
struct sockaddr addr;
socklen_t fromlen = sizeof(addr);
std::string socketError1;

void diep(std::string &socketError1) {
    perror(socketError1.c_str());
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
    memset(chunk_buf, '\0', CONTROLBITLENGTH);
    if(SYNMessage) {
        sprintf(chunk_buf, "SYN %ld %ld", total_packets_to_send, last_packet_size);
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
    memset(chunk_buf, '\0', CONTROLBITLENGTH);
    bool ACKReceived = false;

    while(!ACKReceived) {
        int currentBytesRecieved = 0;
        while(currentBytesRecieved != totalBytesChunk) {
            if((currentBytesRecieved += recvfrom(s, &chunk_buf[currentBytesRecieved], totalBytesChunk -currentBytesRecieved, 0, &addr, &fromlen)) == -1) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                    // handle timeout
                    ////printf("recvfrom() timed out\n");
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
        memset(chunk_buf, '\0', CONTROLBITLENGTH);
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
    char chunk_buf[CONTROLBITLENGTH+1];
    int totalBytesChunk = CONTROLBITLENGTH;

    // Set timeout
    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    timeout.tv_usec = 10000; // and microseconds
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        socketError1 = "setsockopt";
        diep(socketError1);
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
        socketError1 = "setsockopt";
        diep(socketError1);
    }
}

/*
Send FIN packet
Wait for FIN/ACK packet for 1 second else go back to previous step 
Send ACK packet 
Wait for FIN/ACK message for 1 second and if none is recieved continue  
*/

void closeConnection() {
    char chunk_buf[CONTROLBITLENGTH+1];
    int totalBytesChunk = CONTROLBITLENGTH;

    struct timeval timeout;
    timeout.tv_sec = 0;  // timeout in seconds
    timeout.tv_usec = 10000; // and microseconds
    if(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        socketError1 = "setsockopt";
        diep(socketError1);
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
        socketError1 = "setsockopt";
        diep(socketError1);
    }
}

void sendPackets(char *chunk_buf, char *control_buf) {

    // ////printf("sending packets\n");

    // send the current window of packets (window size = CW)
    for(size_t i = 0; i < _floor(CW) && i < packet_arr.size(); i++) {
        memset(chunk_buf, '\0', CONTROLBITLENGTH + PAYLOADSIZE);
        memset(control_buf, '\0', CONTROLBITLENGTH);
        tcp_struct& current_packet = packet_arr[i];
        if(!current_packet.sent_packet) {
            ////printf("sending packet %ld \n", current_packet.seq_num);
            // populate chunk_buf with control bits and payload
            sprintf(control_buf, "SEQ %ld", current_packet.seq_num);
            memcpy(chunk_buf, control_buf, CONTROLBITLENGTH);
            size_t bytes_of_buff_to_send = PAYLOADSIZE;
            memcpy(&chunk_buf[CONTROLBITLENGTH], current_packet.payload_start, bytes_of_buff_to_send);

            // send chunk, and make sure that full chunk is sent via while loop.
            size_t chunk_bytes_sent = 0;
            size_t bytes_to_send_for_chunk = PAYLOADSIZE + CONTROLBITLENGTH;
            while(chunk_bytes_sent != bytes_to_send_for_chunk) {
                // printf("sending\n");
                if((chunk_bytes_sent += sendto(s, &chunk_buf[chunk_bytes_sent], bytes_to_send_for_chunk-chunk_bytes_sent, 0, (struct sockaddr *) &si_other, slen)) == -1) {
                        perror("chunk sending failure.\n");
                        exit(1);
                }
            }
            
            current_packet.sent_packet = true;
            // update current packet timer with the time of send
            current_packet.timer_timestamp_msec = USEC_TO_MSEC(clock());
        }
    }
}


void adjust_timer(timer_t timer_id, time_t new_timeout_val) {

    struct itimerspec new_time;
    new_time.it_value.tv_nsec = MSEC_TO_NSEC(new_timeout_val);

    if(timer_settime(timer_id, 0, &new_time, NULL) == -1) {
        perror("timer_settime in adjust_timer");
        exit(EXIT_FAILURE);
    }
}


void checkTimer() {
    //check if a timeout has occured
    if (timeout_occurred) {
        //printf("sender: timeout occurred \n");
        //reset SST, dupAck
        SST = (int)CW/2;
        dupAckCounter = 0;
        
        // instead of resizing, set all packets in window to false (triggers eventual resend)
        for(size_t i = 0; i < packet_arr.size(); i++) {
            packet_arr[i].sent_packet = false;
        }

        // reset CW
        CW = 1;
        tcp_struct& firstPacket = packet_arr[0];
        firstPacket.timer_timestamp_msec = 0;
        firstPacket.sent_packet = false;

        // move to slow start, restart timer
        currentState = slow_start;
        adjust_timer(tcp_timeout_timer, TIMERTIMEOUT_MSEC);
        //printf("sender: timeout occurred new sequence number %ld\n", packet_arr[0].seq_num);
    }
}

//First time transition: set SST/CW, resend base and expand window if needed 
void transitionToFastRecovery(char *file_buf) {
    //reset state
    currentState = fast_recovery;
    SST = _floor(CW)/2;
    CW = SST + dupAckCounter;
    ////printf("switching to fast recovery and new SST is %ld, and new CW is %f\n", SST, CW);


    //resend first pcket that is causing duplicate ACK
    packet_arr[0].sent_packet = false;
    packet_arr[0].timer_timestamp_msec = 0;

    //slide and expand window for new packets
    //printf("adding (%ld) packets to packet arr.\n", _floor(CW) - packet_arr.size());
    while(packet_arr.size() < _floor(CW) && seqNum < total_packets_to_send && numPacketsCompleted < total_packets_to_send) {
        char* nextAddress = &file_buf[(seqNum)*PAYLOADSIZE];
        seqNum++;
        tcp_struct newEntry = {seqNum, nextAddress, 0, false};
        packet_arr.push_back(newEntry);
        //printf("Added packet %ld\n", seqNum);
    }

}

void slowStart(char *file_buf, size_t ACKSequenceNumber) {
    
    /*
    if recieved ACK is higher than current HACK,
    slide window and inc window size by 1.
    */
    if(ACKSequenceNumber > hack) {
        int slideDistance = 0;
        int tCurrent = 0;
        int tNext = 0;
        //printf("(slow start) recieved ACK is higher than HACK. sliding the congestion window, incrementing CW size.\n");

        //remove acked packet
        while(slideDistance < packet_arr.size() && packet_arr[slideDistance].seq_num < ACKSequenceNumber) {
            slideDistance++;
        }

        if(!packet_arr.empty()) {
            tCurrent = packet_arr[ACKSequenceNumber - (hack + 1)].timer_timestamp_msec;
        }

        numPacketsCompleted += slideDistance+1;

        //int tmp = slideDistance;
        while(!packet_arr.empty() && slideDistance >= 0) {
            //printf("removing packet %ld\n", packet_arr[0].seq_num);
            packet_arr.erase(packet_arr.begin());
            slideDistance--;
        }
        //printf("removed (%d) packets from packet_arr\n", tmp);

        //slide and expand window for new packets
        //printf("slow start add new packets\n");f
        // increase CW based on new highest ACK. 
        CW = CW + (ACKSequenceNumber - hack);
        if(CW >= SST) {
            currentState = congestion_avoidance;
        }
        ////printf("new CW is %f \n", CW);
        //printf("adding (%ld) packets to packet arr.\n", _floor(CW) - packet_arr.size());
        while(packet_arr.size() < _floor(CW) && seqNum < total_packets_to_send && numPacketsCompleted < total_packets_to_send) {
            char* nextAddress = &file_buf[(seqNum)*PAYLOADSIZE];
            seqNum++;
            tcp_struct newEntry = {seqNum, nextAddress, 0, false};
            packet_arr.push_back(newEntry);
            //printf("Added packet %ld\n", seqNum);
        }

        // after the packet window is added in, set the next timer value to the head of the CW window timer
        tNext = packet_arr[0].timer_timestamp_msec;

        // printf("slid window by (%d), completed (%ld) packets total. current packet_arr size: (%ld).\n", tmp, numPacketsCompleted, packet_arr.size());
        
        // calculate new timeout based on single timer equation
        // printf("(slow start) update timer \n");

        // get milliseconds left in timer
        struct itimerspec curr_time_ACK_recieved;
        if(timer_gettime(tcp_timeout_timer, &curr_time_ACK_recieved) == -1) {
            perror("timer_gettime in slow start");
            exit(EXIT_FAILURE);
        }
        int time_remaining_msec = NSEC_TO_MSEC(curr_time_ACK_recieved.it_value.tv_nsec);

        // current time + time remaining
        // clock() + gettime()
        int alarm_time = USEC_TO_MSEC(clock()) + time_remaining_msec;

        // clock()
        clock_t current_ACK_time = USEC_TO_MSEC(clock());

        // new timeout (in relative time)
        new_timeout = (alarm_time - current_ACK_time) + (tNext - tCurrent);

        // adjust timer (threadsafe)
        pthread_mutex_lock(&timer_lock);
        adjust_timer(tcp_timeout_timer, new_timeout);
        pthread_mutex_unlock(&timer_lock);

        // reset dupAck because new hack was recieved
        dupAckCounter = 0;
        hack = ACKSequenceNumber;
    }

    else if(ACKSequenceNumber == hack) {
        dupAckCounter++;
        //printf("new ACK has same value as hack, incrementing dupACK counter to (%ld)", dupAckCounter);
        if(dupAckCounter == DUPACKTHRESHOLD) {
            transitionToFastRecovery(file_buf);
        }
    }
}

void congestionAvoidance(char *file_buf, size_t ACKSequenceNumber) {
    /*/
    if recieved ACK is higher than current HACK,
    slide window and inc window size by 1
    */
    if(ACKSequenceNumber > hack) {
        int slideDistance = 0;
        int tCurrent = 0;
        int tNext = 0;
        //printf("(congestion avoidance) recieved ACK is higher than HACK. sliding the congestion window, incrementing CW size.\n");

        //remove acked packet
        while(slideDistance < packet_arr.size() && packet_arr[slideDistance].seq_num < ACKSequenceNumber) {
            slideDistance++;
        }

        if(!packet_arr.empty()) {
            tCurrent = packet_arr[0].timer_timestamp_msec;
            tNext = packet_arr[0].timer_timestamp_msec;
        }

        numPacketsCompleted += slideDistance+1;
        while(!packet_arr.empty() && slideDistance >= 0) {
            //printf("removing packet %ld\n", packet_arr[0].seq_num);
            packet_arr.erase(packet_arr.begin());
            slideDistance--;
        }
        //printf("removed (%d) packets from packet_arr\n", tmp);

        //slide and expand window for new packets
        //printf("congestion avoidance add new packets\n");
        // increase CW based on equation from lecture. 
        for(size_t i = 0; i < ACKSequenceNumber-hack; i++) {
            CW = CW + 1/_floor(CW);
        }
        ////printf("new CW is %f \n", CW);
        //printf("adding (%ld) packets to packet arr.\n", _floor(CW) - packet_arr.size());
        while(packet_arr.size() < _floor(CW) && seqNum < total_packets_to_send && numPacketsCompleted < total_packets_to_send) {
            char* nextAddress = &file_buf[(seqNum)*PAYLOADSIZE];
            seqNum++;
            tcp_struct newEntry = {seqNum, nextAddress, 0, false};
            packet_arr.push_back(newEntry);
            //printf("Added packet %ld\n", seqNum);
        }

        //printf("slid window by (%d), completed (%ld) packets total. current packet_arr size: (%ld).\n", tmp, numPacketsCompleted, packet_arr.size());
        
        // calculate new timeout based on single timer equation
        //printf("(congestion avoidance) update timer \n");

        // get milliseconds left in timer
        struct itimerspec curr_time_ACK_recieved;
        if(timer_gettime(tcp_timeout_timer, &curr_time_ACK_recieved) == -1) {
            perror("timer_gettime in congestion avoidance");
            exit(EXIT_FAILURE);
        }
        int time_remaining_msec = NSEC_TO_MSEC(curr_time_ACK_recieved.it_value.tv_nsec);

        // current time + time remaining
        // clock() + gettime()
        int alarm_time = USEC_TO_MSEC(clock()) + time_remaining_msec;

        // clock()
        clock_t current_ACK_time = USEC_TO_MSEC(clock());

        // new timeout (in relative time)
        new_timeout = (alarm_time - current_ACK_time) + (tNext - tCurrent);

        // adjust timer (threadsafe)
        pthread_mutex_lock(&timer_lock);
        adjust_timer(tcp_timeout_timer, new_timeout);
        pthread_mutex_unlock(&timer_lock);

        // reset dupAck because new hack was recieved
        dupAckCounter = 0;
        hack = ACKSequenceNumber;
    }

    else if(ACKSequenceNumber == hack) {
        dupAckCounter++;
        //printf("new ACK has same value as hack, incrementing dupACK counter to (%ld)", dupAckCounter);
        if(dupAckCounter == DUPACKTHRESHOLD) {
            transitionToFastRecovery(file_buf);
        }
    }
}

//core function for when currently in fast recovery two cases: Handle another duplicate ACK and Handle newACK 
void fastRecovery(char *file_buf, size_t ACKSequenceNumber) {
    /*
    if recieved ACK is higher than current HACK,
    slide window and inc window size by 1
    */
    if(ACKSequenceNumber > hack) {
        int slideDistance = 0;
        int tCurrent = 0;
        int tNext = 0;
        //printf("(fast recovery) recieved ACK is higher than HACK. Transition out of fast recovery.\n");

        //remove acked packet
        while(slideDistance < packet_arr.size() && packet_arr[slideDistance].seq_num < ACKSequenceNumber) {
            slideDistance++;
        }
        if(!packet_arr.empty()) {
            tCurrent = packet_arr[0].timer_timestamp_msec;
            tNext = packet_arr[0].timer_timestamp_msec;
        }
        numPacketsCompleted += slideDistance+1;

        while(!packet_arr.empty() && slideDistance >= 0) {
            //printf("removing packet %ld\n", packet_arr[0].seq_num);
            packet_arr.erase(packet_arr.begin());
            slideDistance--;
        }
        //printf("removed (%d) packets from packet_arr\n", tmp);

        //reset important variables
        CW = SST;
        dupAckCounter = 0;
        currentState = congestion_avoidance;
        
        //slide and expand window for new packets
        //printf("fast recovery add new packets if possible\n");
        //printf("adding (%ld) packets to packet arr.\n", _floor(CW) - packet_arr.size());
        while(packet_arr.size() < _floor(CW) && seqNum < total_packets_to_send && numPacketsCompleted < total_packets_to_send) {
            char* nextAddress = &file_buf[(seqNum)*PAYLOADSIZE];
            seqNum++;
            tcp_struct newEntry = {seqNum, nextAddress, 0, false};
            packet_arr.push_back(newEntry);
            //printf("Added packet %ld\n", seqNum);
        }

        //printf("slid window by (%d), completed (%ld) packets total. current packet_arr size: (%ld).\n", tmp, numPacketsCompleted, packet_arr.size());
        
        // calculate new timeout based on single timer equation
        printf("(fast recovery) update timer \n");

        // get milliseconds left in timer
        struct itimerspec curr_time_ACK_recieved;
        if(timer_gettime(tcp_timeout_timer, &curr_time_ACK_recieved) == -1) {
            perror("timer_gettime in fast recovery");
            exit(EXIT_FAILURE);
        }
        int time_remaining_msec = NSEC_TO_MSEC(curr_time_ACK_recieved.it_value.tv_nsec);

        // current time + time remaining
        // clock() + gettime()
        int alarm_time = USEC_TO_MSEC(clock()) + time_remaining_msec;

        // clock()
        clock_t current_ACK_time = USEC_TO_MSEC(clock());

        // new timeout (in relative time)
        new_timeout = (alarm_time - current_ACK_time) + (tNext - tCurrent);

        // adjust timer (threadsafe)
        pthread_mutex_lock(&timer_lock);
        adjust_timer(tcp_timeout_timer, new_timeout);
        pthread_mutex_unlock(&timer_lock);

        // reset highest ack recieved 
        hack = ACKSequenceNumber;
    }

    else if(ACKSequenceNumber == hack) {
        //printf("duplicate ACK recieved in fast recovery mode %ld\n", ACKSequenceNumber);
        //increase window by 1 because packets are draining in network
        dupAckCounter++;
        CW +=1;

        //send new packets if possible
        //printf("adding (%ld) packets to packet arr.\n", _floor(CW) - packet_arr.size());
        while(packet_arr.size() < _floor(CW) && seqNum < total_packets_to_send && numPacketsCompleted < total_packets_to_send) {
            char* nextAddress = &file_buf[(seqNum)*PAYLOADSIZE];
            seqNum++;
            tcp_struct newEntry = {seqNum, nextAddress, 0, false};
            packet_arr.push_back(newEntry);
            //printf("Added packet %ld\n", seqNum);
        }
    }
}

void recievePacket(char *control_buf, char *file_buf) {
    //get packet
    int currentBytesRecieved = 0;
    int totalBytesACKChunk = CONTROLBITLENGTH;
    memset(control_buf, '\0', CONTROLBITLENGTH);

    // Set timeout
    struct timeval recvfrom_timeout;
    recvfrom_timeout.tv_usec = MSEC_TO_USEC(new_timeout);
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(s, &read_fds);
    int select_ret = select(s + 1, &read_fds, NULL, NULL, &recvfrom_timeout);

    if(select_ret > 0) {
        //printf("socket recieved ACK\n");
        while(currentBytesRecieved != totalBytesACKChunk) {
            //printf("recieving ACK from socket to control_buf\n");
            if((currentBytesRecieved += recvfrom(s, &control_buf[currentBytesRecieved], totalBytesACKChunk - currentBytesRecieved, 0, &addr, &fromlen)) == -1) {
                    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                        // handle timeout
                        //printf("recvfrom() timed out\n");
                        break;
                    } else {
                        // handle other errors
                        perror("chunk sending failure in phase 2.\n");
                        exit(1);
                    }
            }
        }
    }
    
    size_t ACKSequenceNumber = 0;
    int success = sscanf(control_buf, "ACK %ld", &ACKSequenceNumber);
    if(success != 1) {
        // //printf("ACK not recieved\n");
        // //printf("control buf looks like this: <BOS>%s<EOS>\n", control_buf);
        return;
    } else {
        //printf("ACK recieved for packet (%ld)! control buf looks like this: <BOS>%s<EOS>\n", ACKSequenceNumber, control_buf);
    }

    if(currentState == slow_start) {
        //printf("currently in slow start state\n");
        slowStart(file_buf, ACKSequenceNumber);
    }
    else if(currentState == congestion_avoidance) {
        //printf("currently in congestion avoidance state\n");
        congestionAvoidance(file_buf, ACKSequenceNumber);
    }
    else {
        //printf("currently in fast recovery state\n");
        fastRecovery(file_buf, ACKSequenceNumber);
    }
}


void start_timer(timer_t timer_id, int expiration_msec) {
    struct itimerspec ts;

    // time until timer expires
    ts.it_value.tv_nsec = MSEC_TO_NSEC(expiration_msec);

    // Ensure that ts is zero-initialized
    ts.it_value.tv_sec = 0;

    // Ensure the interval is zero (not periodic)
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;

    // Start timer
    if (timer_settime(timer_id, 0, &ts, 0) == -1) {
        perror("timer_settime in start timer");
        exit(EXIT_FAILURE);
    }
}


void timer_expiration_handler(union sigval sv) {
    pthread_mutex_lock(&timer_lock);
    // printf("timeout handler: timeout occurred!\n");
    timeout_occurred = true;
    pthread_mutex_lock(&timer_lock);
}


void create_timer(void (*handler)(union sigval)) {
    struct sigevent se;

    // Setup sigevent structure
    se.sigev_notify = SIGEV_THREAD;  // Notify using thread
    se.sigev_notify_function = handler;  // Function to execute
    se.sigev_value.sival_ptr = &tcp_timeout_timer;  // Data to pass to handler function
    se.sigev_notify_attributes = NULL;  // Thread attributes (can be NULL)

    // Create timer
    if (timer_create(CLOCK_REALTIME, &se, &tcp_timeout_timer) == -1) {
        perror("timer_create failure in create_timer");
        exit(EXIT_FAILURE);
    }
}


void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    char* file_buf = NULL;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        //printf("Could not open file to send.");
        exit(1);
    }

	/* Determine how many bytes to transfer */
    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        socketError1 = "socket";
        diep(socketError1);
    }

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }
    //printf("socket created\n"):
    
    // read bytesToTransfer bytes from file to buffer
    file_buf = new char[MAXDATASIZE];
    //printf("file buffer address %p\n", file_buf);
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
        delete [] file_buf;
        fclose(fp);
        exit(1);
    }

    // init mutex for timer
    if (pthread_mutex_init(&timer_lock, NULL) != 0) {
        printf("Mutex initialization failed\n");
        exit(1);
    }

    //keep track of total number of packets sent and recieved ack
    total_packets_to_send = _ceil( (double) bytesRead / PAYLOADSIZE);
    last_packet_size = bytesRead % PAYLOADSIZE;

    createConnection();

    //Seperate buffer for control bits like sequence number, startup, and close
    char control_buf[CONTROLBITLENGTH];
    // CONTROLBITLENGTH is the num bits we have in each chunk to enforce TCP
    char chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE+1];
    chunk_buf[CONTROLBITLENGTH + PAYLOADSIZE] = '\0';


    //Capture program time (in microseconds)
    timer = USEC_TO_MSEC(clock());

    // Create a periodic timer
    create_timer(timer_expiration_handler);

    // start timer (timeout timer)
    start_timer(tcp_timeout_timer, TIMERTIMEOUT_MSEC);

    //Load first packet to be sent
    tcp_struct firstPacket = {seqNum, file_buf, 0, false};
    packet_arr.push_back(firstPacket);

    // populate chunk buf from buffer
    while (numPacketsCompleted < total_packets_to_send) {
        ////printf("number of packets completed %ld and total number of packets %ld\n", numPacketsCompleted, total_packets_to_send);
        //check timeout
        checkTimer();

        //send packets
        sendPackets(chunk_buf, control_buf);

        //recieve packets
        recievePacket(control_buf, file_buf);
    }

    //send final messages to close the connection
    closeConnection();

    //printf("Closing the socket\n");
    delete[] file_buf;
    pthread_mutex_destroy(&timer_lock);
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
