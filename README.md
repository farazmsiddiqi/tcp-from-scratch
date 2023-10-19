## Overview
This project builds a fully functioning TCP from a connectionless and unreliable UDP. TCP is implemented fully, including:
- TCP 3-way handshake
- Slow Start
- Congestion Avoidance
- Fast Recovery
- Reciever-side OOO packet buffering
- SST + Timer timeout hyperparams
- custom chunking: payload + control bits in variable length packets
- packet sequence numbers, timer callbacks

This TCP implementation guarantees correctness of packet delivery (in order, all delivered), even with a 99% packet drop rate. 

Packet delivery is efficent, too. A 100KB file is reliably sent over the network with a 50% packet loss in 3.2 seconds (average over 50 trials). This number decreases dramatically as the network packet drop rate decreases. 20MB files (easily create a test from /dev/zero) are transmitted in under 30 seconds. 

This implementation handles delays, OOO packet delivery, packet drops, packet resends, and other common phenomena when sending packets over a network.

## Sender Logic

### Reliable data transfer plan TCP sender

#### Data Structures
SST = 100 (50,000 byte mark) or 64

Duplicate ACK counter (threshold = 3)

`size_t SeqNum = latest packet sequence number`

`size_t HACK = Highest ACK packet sequence number`

`file_buf` is a buf that holds all file data
`file_buf` will be pointed to by a series of `payload_start`.
Each `payload_start` (from `tcp_struct`) corresponds to payload data of a packet.

#### Timer for congestion window: 

`timer_timeout` is an int representing the Timeout threshold, starting at 5 RTTS (100 ms). This timeout will change over time, algo dependent.
`timer` is a clock object that creates a timer and find the elapsed time by subtracting from a new timer multiplying the difference by 1000 and dividing by CLOCKS_PER_SEC

#### Congestion Window (CW)
`CW` starts off at `1`, and is a float (for congestion avoidance).

`CW` defines the size of `packet_arr`, an array of `tcp_struct` objects.

`packet_arr` is a vector (yes, we implemented a simple vector class). The start of the vector represents the head of the congestion window, and the end represents the tail of the congestion window.


#### Global state tracking
ENUM for sender state: Slow start, Congestion avoidance, Fast recovery

#### TCP Struct
This struct will represent a packet, and pointers to these structs will be held in the `packet_arr`.
```
struct tcp_struct {
    seq_num;
    char* payload_start;
    int timer_timestamp;
}
```

### Algorithms (Sender side)

#### UNIVERSAL

**Timeout**

Check if packet in front of window has reached timer threshold, set SST to be half of CW, truncate CW to be 1 and clear timestamp, set duplicate ACK counter to zero and set mode to SLOW START. Restart timer.

**Send Packets**

Iterate through `packet_arr` (of size `CW`). If packet hasn't be sent then send packets and then update timestamp after sent. 

#### SLOW START 

**Recieve new ACK:**

Check if ACK sequence number is higher than HACK. 

If so, mark all packets with sequence number <= newest ACK as completed, restart timer using leftmost packet: timestamp of leftmost packet sent + timer interval - packet ack time (current time) + (timestamp of next packet sent - timestamp of leftmost packet sent) and slide packet window, reset HACK to newest ACK and set dup ack count to 0. 

If so, update `CW` to ```CW_curr + (ACK_new - HACK)```. Expand `packet_arr` to reflect `CW` change. 

Populate the new packets to be sent. 

**DupAck**:

Recieve duplicate ACK: Check if ACK sequence number is == HACK and increment duplicate ACK counter.

#### Fast Recovery:
If duplicate ACK counter reaches threshold switch to Fast recovery state. Then, Set SST equal to CW/2.
Next, set Congestion window size and expand or contract window equal to SST + duplicate ACK count. Re-send packet at the head of the sender window. Slow start to congestion avoidance: If congestion window size reaches SST then switch to congestion window state (ENUM). 

#### CONGESTION AVOIDANCE 
Recieve new ACK: Check if ACK sequence number is higher than HACK, next expand congestion window and window 
size variable by current congestion window size + 1/floor(congestion window size) and repeat (highest ACK seen - new ACK 
sequence number) times. Set highest ACK seen and mark all packets with sequence number <= newest ACK
as completed and shift congestion window. Set duplicate ACK counter to zero.
Recieve duplicate ACK: Same as process in slow start. 

FAST RECOVERY
Recieve new ACK: Check if ACK sequence number is higher than HACK, next expand congestion window to SST.
Set highest ACK seen and mark all packets with sequence number <= newest ACK as completed and shift congestion window. Set duplicate ACK counter to zero.
Recieve duplicate ACK: Check if ACK sequence number is <= HACK and increment duplicate ack counter. Next,
Next expand congestion window by 1. 

## Reciever Logic

Reliable data transfer TCP reciever 
**Data structures**

Highest in order packet recieved sequence number

Unlimited size out of order packet buffering of packets (sequence number and payload of packet) 

**Algorithmns** 

Recieve new in-order packet: Check if sequence number = highest-in order packet recieved counter+1. 
Next increment highest in-order packet recieved counter. Check if OOO buffer can be emptied and update
highest in-order packet recieved counter. Write packets to data array based on packet recieved and 
emptied packets from OOO buffer and Send ACK for highest in-order packet.

Recieve out of-order packet: Check if sequence number > highest-in order packer recieved counter+1. 
Next store packet contents in out of order array in the correct position. Positions go from highest in-order 
packet recieved to N. Send ACK for current highest in-order packet recieved 
Ignore packet with sequence number less than highest in-order packet
