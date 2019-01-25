#include "ns.h"

extern union Nsipc nsipcbuf;


void
input(envid_t ns_envid)
{
    binaryname = "ns_input";

    // LAB 6: Your code here:
    //  - read a packet from the device driver
    //  - send it to the network server
    // Hint: When you IPC a page to the network server, it will be
    // reading from it for a while, so don't immediately receive
    // another packet in to the same physical page.

    int32_t value;
    int32_t len;
 

    while(1) {
        char buffer[2048];
        while ( (len = sys_receive_packet(buffer, 2048)) < 0) {
            sys_yield();
        }

		nsipcbuf.pkt.jp_len = len;

        memmove(nsipcbuf.pkt.jp_data, buffer, len);

        while ((value = sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_W | PTE_U)) < 0) {
            int32_t time = sys_time_msec();
            while (sys_time_msec()-time < 1){sys_yield();}
        }
    }
}