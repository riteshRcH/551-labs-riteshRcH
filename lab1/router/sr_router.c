/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    pthread_detach(thread);
    
    /* Add initialization code here! */

} /* -- sr_init -- */

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

  printf("*** -> Received packet of length %d \n",len);

  if (ethertype_arp == ethertype(packet))
    process_arp_packet(sr, packet, len, interface);
  else if (ethertype_ip == ethertype(packet))
    process_ip_packet(sr, packet, len, interface);
}/* end sr_ForwardPacket */


void process_ip_packet(struct sr_instance *sr,
                      uint8_t *packet/* lent */,
                      unsigned int len,
                      char *interface/* lent */)
{
    if (sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t) > len) {
        fprintf(stderr, "Error! Cannot process IP packet because it was not long enough.\n");
        return;
    }

    sr_ip_hdr_t *received_ip_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
    sr_icmp_hdr_t *original_icmp_header = (sr_icmp_hdr_t *)(packet + sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t));
    /* Check IP Header cksum */
    uint16_t original_chksum = received_ip_hdr->ip_sum;
    received_ip_hdr->ip_sum = 0;
    received_ip_hdr->ip_sum = cksum(received_ip_hdr, sizeof(sr_ip_hdr_t));
    if (received_ip_hdr->ip_sum != original_chksum) {
        fprintf(stderr, "Checksum of IP header failed on recomputation\n");
        received_ip_hdr->ip_sum = original_chksum;
        return;
    }

    struct sr_if *dest_intf = get_interface_from_ip(sr, received_ip_hdr->ip_dst);
    if (dest_intf) {
        if (ip_protocol_icmp == received_ip_hdr->ip_p) {
            if (sizeof(sr_icmp_hdr_t) + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) > len) {
                fprintf(stderr, "ICMP packet not sufficient length to process\n");
                return;
            }
            
            fprintf(stdout, "%s received a packet destined for it.\n", interface);
            original_chksum = original_icmp_header->icmp_sum;
            original_icmp_header->icmp_sum = 0;
            original_icmp_header->icmp_sum = cksum(original_icmp_header, len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));

            if (original_icmp_header->icmp_sum != original_chksum) {
                fprintf(stderr, "Checksum of ICMP header failed on recomputation\n");
                original_icmp_header->icmp_sum = original_chksum;
                return;
            }

            if (original_icmp_header->icmp_type != 8) {
                fprintf(stderr, "Packet is not of type of echo request\n");
                return;
            }

            fprintf(stdout, "Packet received by %s was an echo request. Sending echo reply to just received echo request.\n", interface);
            send_fabricated_icmp_packet(sr, packet, len, interface, 0x00, 0x00, dest_intf);
            return;
        } else {  /*Packet is not ICMP.*/
            send_fabricated_icmp_packet(sr, packet, len, interface, 3, 3, dest_intf);
            return;
        }
    } else {  /* Packet was not for one of my interfaces */
        fprintf(stderr, "Received packet on interface %s that is not mine and needs to be forwarded\n", interface);

        if (1 >= received_ip_hdr->ip_ttl) {
            fprintf(stderr, "Sending icmp11 due to ttl expiry\n");
            send_fabricated_icmp_packet(sr, packet, len, interface, 11, 0, NULL);
            return;
        }

        /* Packet is not for this router and has valid TTL. Forward the packet. */
        fprintf(stderr, "Forwarding packet that was received on interface %s\n", interface);

        struct sr_rt *routing_table_node = sr->routing_table, *best_match = NULL;
	    while (routing_table_node) {
	        if ((routing_table_node->dest.s_addr & routing_table_node->mask.s_addr) == (received_ip_hdr->ip_dst & routing_table_node->mask.s_addr)) {
	            if (!best_match || (routing_table_node->mask.s_addr > best_match->mask.s_addr)) {
	                best_match = routing_table_node;
	            }
	        }
	        routing_table_node = routing_table_node->next;
	    }

        struct sr_rt *next_hop_ip = best_match;

        if (!next_hop_ip) { /* No match found in routing table */
            fprintf(stderr, "Sending ICMP3 since no match found in routing table\n");
            send_fabricated_icmp_packet(sr, packet, len, interface, 3, 0, NULL);
            return;
        }

        received_ip_hdr->ip_ttl--, received_ip_hdr->ip_sum = 0, received_ip_hdr->ip_sum = cksum(received_ip_hdr, sizeof(sr_ip_hdr_t));
        struct sr_arpentry *next_hop_mac = sr_arpcache_lookup(&(sr->cache), next_hop_ip->gw.s_addr);
        if (!next_hop_mac) { /* No ARP cache entry found */
            fprintf(stderr, "ARP request saved for later since no cache entry found\n");
            struct sr_arpreq *queued_arp_req = sr_arpcache_queuereq(&(sr->cache), next_hop_ip->gw.s_addr /*received_ip_hdr->ip_dst*/,
                                               packet, len, next_hop_ip->interface);
            process_arpreq(sr, queued_arp_req);
            return;
        }
        fprintf(stderr, "Sending packet on interface %s to be sent to next hop since ARP cache entry was found\n", next_hop_ip->interface);
        sr_ethernet_hdr_t *sending_eth_hdr = (sr_ethernet_hdr_t *)packet;

        memcpy(sending_eth_hdr->ether_dhost, next_hop_mac->mac, sizeof(uint8_t) * ETHER_ADDR_LEN);
        memcpy(sending_eth_hdr->ether_shost, sr_get_interface(sr, next_hop_ip->interface)->addr, sizeof(uint8_t) * ETHER_ADDR_LEN);
        
        free(next_hop_mac);

        sr_send_packet(sr, packet, len, sr_get_interface(sr, next_hop_ip->interface)->name);
        return;
    }
}

void send_fabricated_icmp_packet(struct sr_instance *sr, uint8_t *packet, unsigned int len,
                             char *got_on_intf, uint8_t icmp_type, uint8_t icmp_code, struct sr_if *dest_intf)
{
    int hdr_length = sizeof(sr_icmp_hdr_t) + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t);
    int sending_length = 0x00 == icmp_type ? len : hdr_length;
    
    uint8_t *icmp = (uint8_t *)malloc(sending_length);
    memset(icmp, 0, sizeof(uint8_t) * sending_length);

    sr_ethernet_hdr_t *sending_eth_hdr = (sr_ethernet_hdr_t *)icmp;
    sr_icmp_hdr_t *send_icmp_header = (sr_icmp_hdr_t *)(icmp + sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t));
    sr_ip_hdr_t *received_ip_hdr = (sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
    sr_ip_hdr_t *send_ip_header = (sr_ip_hdr_t *)(icmp + sizeof(sr_ethernet_hdr_t));
    sr_ethernet_hdr_t *given_eth_hdr = (sr_ethernet_hdr_t *)packet;
    struct sr_if *outgoing_interface = sr_get_interface(sr, got_on_intf);

    uint32_t src_ip = outgoing_interface->ip;
    if (dest_intf) {
        src_ip = dest_intf->ip;
    }
    
    if (0x00 == icmp_type)
        memcpy(send_icmp_header, (sr_icmp_hdr_t *)(icmp + sizeof(sr_ip_hdr_t) + sizeof(sr_ethernet_hdr_t)), sending_length - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t));
    else
        memcpy(send_icmp_header->data, received_ip_hdr, ICMP_DATA_SIZE);

    send_icmp_header->icmp_sum = 0x00 == icmp_type ? cksum(send_icmp_header, len - sizeof(sr_ethernet_hdr_t) - sizeof(sr_ip_hdr_t)) : cksum(send_icmp_header, sizeof(sr_icmp_hdr_t));

    send_icmp_header->icmp_type = icmp_type, send_icmp_header->icmp_code = icmp_code, send_icmp_header->icmp_sum = 0;

    memcpy(send_ip_header, received_ip_hdr, sizeof(sr_ip_hdr_t));
    send_ip_header->ip_ttl = INIT_TTL, send_ip_header->ip_p = ip_protocol_icmp, send_ip_header->ip_len = htons(sending_length - sizeof(sr_ethernet_hdr_t)), send_ip_header->ip_src = src_ip, send_ip_header->ip_dst = received_ip_hdr->ip_src, send_ip_header->ip_sum = 0, send_ip_header->ip_sum = cksum(send_ip_header, sizeof(sr_ip_hdr_t));
    
    memcpy(sending_eth_hdr->ether_dhost, given_eth_hdr->ether_shost, sizeof(uint8_t) * ETHER_ADDR_LEN);
    memcpy(sending_eth_hdr->ether_shost, outgoing_interface->addr, sizeof(uint8_t) * ETHER_ADDR_LEN);
    sending_eth_hdr->ether_type = htons(ethertype_ip);

    sr_send_packet(sr, icmp, sending_length, got_on_intf);

    free(icmp);

    return;
}

void process_arp_packet(struct sr_instance *sr, uint8_t *packet, unsigned int len, char *interface)
{
    if (sizeof(sr_arp_hdr_t) + sizeof(sr_ethernet_hdr_t) > len) {
        fprintf(stderr, "Insufficient length for ARP packet\n");
        return;
    }

    struct sr_if *got_on_intf = sr_get_interface(sr, interface);

    sr_ethernet_hdr_t *given_eth_hdr = (sr_ethernet_hdr_t *)packet;

    sr_arp_hdr_t *given_arp_hdr = (sr_arp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));

    if (arp_op_request == ntohs(given_arp_hdr->ar_op)) {

        if (given_arp_hdr->ar_tip != got_on_intf->ip)
            return;

        fprintf(stdout, "Received ARP request on interface %s\n", interface);

        uint8_t *arp_reply = (uint8_t *) malloc(len);
        memset(arp_reply, 0, len * sizeof(uint8_t));

        sr_ethernet_hdr_t *reply_eth_hdr = (sr_ethernet_hdr_t *)arp_reply;
        memcpy(reply_eth_hdr->ether_dhost, given_eth_hdr->ether_shost, sizeof(uint8_t) * ETHER_ADDR_LEN);
        memcpy(reply_eth_hdr->ether_shost, got_on_intf->addr, sizeof(uint8_t) * ETHER_ADDR_LEN);
        reply_eth_hdr->ether_type = htons(ethertype_arp);
        
        sr_arp_hdr_t *reply_arp_hdr = (sr_arp_hdr_t *)(arp_reply + sizeof(sr_ethernet_hdr_t));
        memcpy(reply_arp_hdr, given_arp_hdr, sizeof(sr_arp_hdr_t));
        reply_arp_hdr->ar_op = htons(arp_op_reply);
        memcpy(reply_arp_hdr->ar_sha, got_on_intf->addr, ETHER_ADDR_LEN);
        memcpy(reply_arp_hdr->ar_tha, given_eth_hdr->ether_shost, ETHER_ADDR_LEN);
        reply_arp_hdr->ar_sip = got_on_intf->ip;
        reply_arp_hdr->ar_tip = given_arp_hdr->ar_sip;

        sr_send_packet(sr, arp_reply, len, interface);

        free(arp_reply);

    } else if (arp_op_reply == ntohs(given_arp_hdr->ar_op)) {
        fprintf(stdout, "Got ARP reply on interface %s\n", interface);

        struct sr_arpreq *cached_arp_request = sr_arpcache_insert(&(sr->cache),
                                               given_arp_hdr->ar_sha,
                                               given_arp_hdr->ar_sip);
        if (cached_arp_request) {
            struct sr_packet *in_wait_queue_packet = cached_arp_request->packets;
            while (in_wait_queue_packet) {
                uint8_t *send_packet = in_wait_queue_packet->buf;

                sr_ethernet_hdr_t *sending_eth_hdr = (sr_ethernet_hdr_t *)send_packet;
                memcpy(sending_eth_hdr->ether_shost, got_on_intf->addr, ETHER_ADDR_LEN);
                memcpy(sending_eth_hdr->ether_dhost, given_arp_hdr->ar_sha, ETHER_ADDR_LEN);
                
                sr_send_packet(sr, send_packet, in_wait_queue_packet->len, interface);
                
                in_wait_queue_packet = in_wait_queue_packet->next;
            }
            sr_arpreq_destroy(&(sr->cache), cached_arp_request);
        }
    }
    return;
}

struct sr_if *get_interface_from_ip(struct sr_instance *sr, uint32_t ip_address)
{
    struct sr_if *intf = sr->if_list, *dest_intf = NULL;
    while (intf) {
        if (ip_address == intf->ip) {
            dest_intf = intf;
            break;
        }
        intf = intf->next;
    }
    return dest_intf;
}