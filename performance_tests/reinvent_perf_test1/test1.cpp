//                                                                                                                      
// Tell GCC to not enforce '-Wpendantic' for DPDK headers                                                               
//                                                                                                                      
#pragma GCC diagnostic push                                                                                             
#pragma GCC diagnostic ignored "-Wpedantic"                                                                             
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_mbuf_ptype.h>
#include <rte_rawdev.h>
#include <rte_per_lcore.h>
#pragma GCC diagnostic pop 

//
// Clients send this. Servers get it
//
struct TxMessage {
  int      lcoreId;   // lcoreId which sent this message
  int      txqId;     // txqId which sent this message
  unsigned sequence;  // txqId's sequence number
  char     pad[20];   // to fill out message to 32 bytes
};

struct LocalTxState {
  rte_ether_addr  srcMac;
  rte_ether_addr  dstMac;
  uint16_t        srcPort;
  uint16_t        dstPort;
  uint32_t        srcIp;
  uint32_t        dstIp;
  uint32_t        sequence;
  int32_t         lcoreId;
  int32_t         txqId;
  uint32_t        count;
}; 

int clientMainLoop(int id, int txqIndex, unsigned packetCount, rte_mempool *pool, rte_ether_addr srcMac,
  rte_ether_addr dstMac, uint16_t srcPort, uint16_t dstPort, uint32_t srcIp, uint32_t dstIp) {

  LocalTxState state __attribute__ ((aligned (64)));
  memset(&state, 0, sizeof(LocalTxState));

  //
  // Device Id
  //
  const uint16_t deviceId = static_cast<uint16_t>(config->awsEnaConfig().deviceId());

  //
  // IPV4 UDP for this demo
  //
  const uint16_t ethFlow = rte_cpu_to_be_16(static_cast<uint16_t>(config->awsEnaConfig().txq()[txqIndex].defaultFlow()));

  //
  // Convert src MAC address from string to binary
  //
  state.srcMac = srcMac;

  //
  // Convert dst MAC address from string to binary
  //
  state.dstMac = dstMac;

  //
  // Convert src IP address from string to binary
  //
  state.srcIp = srcIp;

  //
  // Convert dst IP address from string to binary
  //
  state.dstIp = dstIp;

  //
  // UDP pseudo ports
  //
  state.srcPort = srcPort;
  state.dstPort = dstPort;

  //
  // Total all-in packet size sent
  //
  const int packetSize = sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr)+sizeof(rte_udp_hdr)+sizeof(TxMessage);

  //
  // Size relative to IP4V header
  //
  const uint16_t ip4Size = rte_cpu_to_be_16(static_cast<uint16_t>(sizeof(rte_ipv4_hdr)+sizeof(rte_udp_hdr)+sizeof(TxMessage)));

  //
  // Size relative to UDP header
  //
  const uint16_t udpSize = rte_cpu_to_be_16(static_cast<uint16_t>(sizeof(rte_udp_hdr)+sizeof(TxMessage)));

  const unsigned max = packetCount;

  state.lcoreId = id;
  state.txqId = txqIndex;

  rte_mbuf * mbuf(0);
 
  while (state.count<max) {
    if (unlikely((mbuf = rte_pktmbuf_alloc(pool))==0)) {
      printf("failed to allocate mbuf\n");
      return 0;
    }

    // Within a given mbuf:
    //
    //  +---> rte_pktmbuf_mtod_offset(mbuf, rte_ether_hdr*, 0) 
    // /
    // +---------------+--------------+-------------+-----------+
    // | rte_ether_hdr | rte_ipv4_hdr | rte_udp_hdr | TxMessage |   
    // +---------------+--------------+-------------+-----------+
    //

    //
    // Prepare ether header
    //
    struct rte_ether_hdr *ethHdr = rte_pktmbuf_mtod_offset(mbuf, rte_ether_hdr*, 0);
    ethHdr->src_addr = state.srcMac;
    ethHdr->dst_addr = state.dstMac;
    ethHdr->ether_type = ethFlow;

    //
    // Prepare IPV4 header
    //
    rte_ipv4_hdr *ip4Hdr = rte_pktmbuf_mtod_offset(mbuf, rte_ipv4_hdr*, sizeof(rte_ether_hdr));
    ip4Hdr->ihl = 0x5;
    ip4Hdr->version = 0x04;
    ip4Hdr->type_of_service = 0;
    ip4Hdr->total_length = ip4Size;
    ip4Hdr->packet_id = 0;
    ip4Hdr->fragment_offset = 0;
    ip4Hdr->time_to_live = IPDEFTTL;
    ip4Hdr->next_proto_id = IPPROTO_UDP;
    ip4Hdr->hdr_checksum = 0;
    ip4Hdr->src_addr = state.srcIp;
    ip4Hdr->dst_addr = state.dstIp;

    //
    // Prepare UDP header
    //
    rte_udp_hdr *udpHdr = rte_pktmbuf_mtod_offset(mbuf, rte_udp_hdr*,
      sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr));
    udpHdr->src_port = state.srcPort;
    udpHdr->dst_port = state.dstPort;
    udpHdr->dgram_len = udpSize;
    udpHdr->dgram_cksum = 0;
  
    //
    // Prepare TxMessage Payload
    //
    TxMessage *payload = rte_pktmbuf_mtod_offset(mbuf, TxMessage*,
      sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr)+sizeof(rte_udp_hdr));
    payload->lcoreId = state.lcoreId;
    payload->txqId = state.txqId;
    payload->sequence = ++state.sequence;

    //
    // Finalize mbuf in DPDK
    //
    mbuf->nb_segs = 1;
    mbuf->pkt_len = packetSize;
    mbuf->data_len = packetSize;
    mbuf->ol_flags = (RTE_MBUF_F_TX_IPV4|RTE_MBUF_F_TX_IP_CKSUM);
    mbuf->l2_len = sizeof(rte_ether_hdr);
    mbuf->l3_len = sizeof(rte_ipv4_hdr);
    mbuf->l4_len = sizeof(rte_udp_hdr);
    mbuf->packet_type = (RTE_PTYPE_L2_ETHER|RTE_PTYPE_L3_IPV4|RTE_PTYPE_L4_UDP);

    if (unlikely(0==rte_eth_tx_burst(deviceId, state.txqId, &mbuf, 1))) {
      while(1!=rte_eth_tx_burst(deviceId, state.txqId, &mbuf, 1));
    }
    ++state.count;
  }

  return 0;
}

int main(int argc, char **argv) {
  int rc = rte_eal_init(static_cast<int>(argc, argvc);
  if (rc!=0) {
    fprintf(stderr, "rte_eal_init failed rc=%d\n", rc);
    return rc;
  }

  rte_mempool *pool = rte_pktmbuf_pool_create("test1", 512, 0, 0, 1024);
  if (pool==0) {
    fprintf(stderr, "rte_pktmbuf_pool_create failed\n");
    rte_eal_cleanup();
  }

  rte_ether_addr srcMac;
  rte_ether_addr dstMac;
  srcMac.add_bytes = {0};
  dstMac.add_bytes = {1};
  uint16_t srcPort = 10000;
  uint16_t dstPort = 20000;
  uint32_t srcIp = 0x12345678;
  uint32_t dstIp = 0x12345678;

  rc = clientMainLoop(0, 0, 10000, pool, srcMac, dstMac, srcPort, dstPort, srcIp, dstIp);
    
  rte_eal_cleanup();

  return rc;
}
