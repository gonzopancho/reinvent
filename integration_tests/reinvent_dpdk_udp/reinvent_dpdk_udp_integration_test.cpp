#include <dpdk/reinvent_dpdk_initaws.h>
#include <dpdk/reinvent_dpdk_uninitaws.h>
#include <dpdk/reinvent_dpdk_awsworker.h>
#include <dpdk/reinvent_dpdk_util.h>
#include <dpdk/reinvent_dpdk_stream.h>

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

#include <time.h>
#include <unistd.h>
#include <signal.h>

//
// Default burst capacities
//
unsigned RX_BURST_CAPACITY = 15;
const unsigned TX_BURST_CAPACITY = 8;

unsigned SLEEP_TIME = 10;
unsigned REPORT_COUNT = 500000;

//
// See -P argument
//
bool constantPorts(true);

//
// Set true to terminate RXQ
//
static volatile int terminate = 0;

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
  uint32_t        count;
  uint32_t        sent;
  uint32_t        left;
  uint32_t        packetCount;
  uint16_t        i;
  int             lcoreId;
  int             txqId;
  unsigned        sequence;
}; 

RTE_DEFINE_PER_LCORE(LocalTxState, localTxState __attribute__ ((aligned (64)))) = {
  .srcMac       = {0},
  .dstMac       = {0},
  .srcPort      = 0,
  .dstPort      = 0,
  .count        = 0,
  .sent         = 0,
  .left         = 0,
  .packetCount  = 0,
  .i            = 0,
  .lcoreId      = 0,
  .txqId        = 0,
  .sequence     = 0
};

static RTE_DEFINE_PER_LCORE(rte_mbuf *[8], TxBurstCache __attribute__ ((aligned (64))));

static void handle_sig(int sig) {
  switch (sig) {
    case SIGINT:
    case SIGTERM:
      terminate = 1;
      break;
  }
}

uint64_t timeDifference(timespec start, timespec end) {
  uint64_t diff = static_cast<uint64_t>(end.tv_sec)*static_cast<uint64_t>(1000000000)+static_cast<uint64_t>(end.tv_nsec);
  diff -= (static_cast<uint64_t>(start.tv_sec)*static_cast<uint64_t>(1000000000)+static_cast<uint64_t>(start.tv_nsec));
  return diff;
}

void internalExit(const Reinvent::Dpdk::AWSEnaConfig& config) {
  if (Reinvent::Dpdk::UnInitAWS::device(config)!=0) {
    REINVENT_UTIL_LOG_WARN_VARGS("Cannot uninitialize AWS ENA device\n");
  }

  if (Reinvent::Dpdk::UnInitAWS::ena()!=0) {
    REINVENT_UTIL_LOG_WARN_VARGS("Cannot uninitialize DPDK system\n");
  }
}

void onExit(const Reinvent::Dpdk::AWSEnaConfig& config) {
  //
  // Print the report link speed
  //
  rte_eth_link linkStatus;
  rte_eth_link_get_nowait(config.deviceId(), &linkStatus);
  printf("linkRate value: %u\n", linkStatus.link_speed);

  //
  // Get high level stats. Unfortuantely this can only report up
  // to RTE_ETHDEV_QUEUE_STAT_CNTRS queues which is 16. But
  // AWS ENA NICs can have up to 32 queues. So for queue stats we
  // use the xstats API 
  //
  rte_eth_stats stats;
  memset(&stats, 0, sizeof(stats));
  rte_eth_stats_get(config.deviceId(), &stats);

  //
  // Dump high level stats
  //
  printf("in  packets : %lu\n", stats.ipackets);
  printf("out packets : %lu\n", stats.opackets);
  printf("in  bytes   : %lu\n", stats.ibytes);
  printf("out bytes   : %lu\n", stats.obytes);
  printf("missed pkts : %lu\n", stats.imissed);
  printf("in err pkts : %lu\n", stats.ierrors);
  printf("out err pkts: %lu\n", stats.oerrors);
  printf("rx allc errs: %lu\n", stats.rx_nombuf);

  rte_eth_xstat *xstats;
  rte_eth_xstat_name *xstats_names;
  int len = rte_eth_xstats_get(config.deviceId(), 0, 0);
  if (len < 0) {
    printf("warn: cannot get xstats\n");
    internalExit(config);
  }

  xstats = static_cast<rte_eth_xstat*>(calloc(len, sizeof(*xstats)));
  if (xstats == 0) {
    printf("warn: out of memory\n");
    internalExit(config);
  }
  int ret = rte_eth_xstats_get(config.deviceId(), xstats, len);
  if (ret<0||ret>len) {
    free(xstats);
    printf("warn: rte_eth_xstats_get unexpected result\n");
    internalExit(config);
  }
  xstats_names = static_cast<rte_eth_xstat_name*>(calloc(len, sizeof(*xstats_names)));
  if (xstats_names == 0) {
    free(xstats);
    printf("warn: out of memory\n");
    internalExit(config);
  }
  ret = rte_eth_xstats_get_names(config.deviceId(), xstats_names, len);
  if (ret<0||ret > len) {
    free(xstats);
    free(xstats_names);
    printf("warn: rte_eth_xstats_get_names unexpected result\n");
    internalExit(config);
  }
  for (int i=0; i<len; i++) {
    printf("%-32s: %lu\n", xstats_names[i].name, xstats[i].value);
  }

  free(xstats);
  free(xstats_names);

  internalExit(config);
}

void usageAndExit() {
  printf("reinvent_dpdk_udp_integration_test.tsk -m <client|server> -p <env-var-prefix>\n");
  printf("   -m <client|server>       optional (default server). mode to run task in.\n");
  printf("   -p <string>              required: non-empty ENV variable prefx name with config\n");
  printf("   -r <integer>             optional: per RXQ burst capacity default %d\n", RX_BURST_CAPACITY);
  printf("   -P                       increment src/dst ports for each TX packet sent\n");
  printf("                            this option helps RSS use more queues by changing cksum\n");
  printf("   -S <integer>             number of seconds to sleep after RX/TX work done before lcore exits\n");
  printf("                            default %d seconds. See https://github.com/amzn/amzn-drivers/issues/166\n", SLEEP_TIME);
  printf("   -R <integer>             RXQ lcores report stats every <integer> packets received. default %d\n", REPORT_COUNT);
  exit(2);
}

void parseCommandLine(int argc, char **argv, bool *isServer, std::string *prefix) {
  int c, n;

  *isServer = true;
  prefix->clear();

  while ((c = getopt (argc, argv, "m:p:r:PS:R:")) != -1) {
    switch(c) {
      case 'm':
        if (strcmp(optarg, "server")==0) {
          *isServer = true;
        } else if (strcmp(optarg, "client")==0) {
          *isServer = false;
        } else {
          usageAndExit();
        }
        break;
      case 'p':
        *prefix = optarg;
        break;
      case 'r':
        n = atoi(optarg);
        if (n<=0) {
          usageAndExit();
        }
        RX_BURST_CAPACITY = static_cast<unsigned>(n);
        printf("RX_BURST_CAPACITY set to %u\n", RX_BURST_CAPACITY);
        break;
      case 'P':
        constantPorts=false;
        break;
      case 'S':
        n = atoi(optarg);
        if (n<=0) {
          usageAndExit();
        }
        SLEEP_TIME = static_cast<unsigned>(n);
        printf("SLEEP_TIME set to %u\n", SLEEP_TIME);
        break;
      case 'R':
        n = atoi(optarg);
        if (n<=0) {
          usageAndExit();
        }
        REPORT_COUNT = static_cast<unsigned>(n);
        printf("REPORT_COUNT set to %u\n", REPORT_COUNT);
        break;
      default:
        usageAndExit();
    }
  }

  if (prefix->empty()) {
    usageAndExit();
 }
}

int clientMainLoop(int id, int txqIndex, Reinvent::Dpdk::AWSEnaWorker *config, unsigned packetCount) {

  assert(config);

  rte_mbuf **mbuf;
  mbuf = RTE_PER_LCORE(TxBurstCache);

  LocalTxState *state;
  state = &RTE_PER_LCORE(localTxState);
  state->packetCount = packetCount;
  state->lcoreId = id;
  state->txqId = txqIndex;
  state->sequence = 0;

  printf("sizeof(LocalTxState)==%lu\n", sizeof(LocalTxState));

  //
  // Device Id
  //
  const uint16_t deviceId = static_cast<uint16_t>(config->awsEnaConfig().deviceId());

  //
  // Find the TXQ's mempool
  //
  rte_mempool *pool = config->awsEnaConfig().txq()[txqIndex].mempool();
  assert(pool);

  //
  // IPV4 UDP for this demo
  //
  const uint16_t ethFlow = rte_cpu_to_be_16(static_cast<uint16_t>(config->awsEnaConfig().txq()[txqIndex].defaultFlow()));

  //
  // Convert src MAC address from string to binary
  //
  config->awsEnaConfig().txq()[txqIndex].defaultRoute().convertSrcMac(&(state->srcMac));

  //
  // Convert dst MAC address from string to binary
  //
  config->awsEnaConfig().txq()[txqIndex].defaultRoute().convertDstMac(&(state->dstMac));

  //
  // Convert src IP address from string to binary
  //
  uint32_t srcIp(0);
  config->awsEnaConfig().txq()[txqIndex].defaultRoute().convertSrcIp(&srcIp);

  //
  // Convert dst IP address from string to binary
  //
  uint32_t dstIp(0);
  config->awsEnaConfig().txq()[txqIndex].defaultRoute().convertDstIp(&dstIp);

  //
  // UDP pseudo ports
  //
  state->srcPort = static_cast<uint16_t>(config->awsEnaConfig().txq()[txqIndex].defaultRoute().srcPort());
  state->dstPort = static_cast<uint16_t>(config->awsEnaConfig().txq()[txqIndex].defaultRoute().dstPort());

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

  struct timespec start;
  clock_gettime(CLOCK_REALTIME, &start);
 
  while (state->sequence<state->packetCount) {
    //
    // Allocate 'TX_BURST_CAPACITY' mbufs
    //
    for(state->i=0; state->i<TX_BURST_CAPACITY; ++state->i) {
      if ((mbuf[state->i] = rte_pktmbuf_alloc(pool))==0) {
        printf("failed to allocate mbuf\n");
        return 0;
      }
    }

    //
    // Prepare and queue for write TX_BURST_CAPACITY packets
    //
    for(state->i=0; state->i<TX_BURST_CAPACITY; ++state->i) {
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
      struct rte_ether_hdr *ethHdr = rte_pktmbuf_mtod_offset(mbuf[state->i], rte_ether_hdr*, 0);
      ethHdr->src_addr = state->srcMac;
      ethHdr->dst_addr = state->dstMac;
      ethHdr->ether_type = ethFlow;

      //
      // Prepare IPV4 header
      //
      rte_ipv4_hdr *ip4Hdr = rte_pktmbuf_mtod_offset(mbuf[state->i], rte_ipv4_hdr*, sizeof(rte_ether_hdr));
      ip4Hdr->ihl = 0x5;
      ip4Hdr->version = 0x04;
      ip4Hdr->type_of_service = 0;
      ip4Hdr->total_length = ip4Size;
      ip4Hdr->packet_id = 0;
      ip4Hdr->fragment_offset = 0;
      ip4Hdr->time_to_live = IPDEFTTL;
      ip4Hdr->next_proto_id = IPPROTO_UDP;
      ip4Hdr->hdr_checksum = 0;
      ip4Hdr->src_addr = srcIp;
      ip4Hdr->dst_addr = dstIp;

      //
      // Prepare UDP header
      //
      rte_udp_hdr *udpHdr = rte_pktmbuf_mtod_offset(mbuf[state->i], rte_udp_hdr*,
        sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr));
      udpHdr->src_port = rte_cpu_to_be_16(state->srcPort);
      udpHdr->dst_port = rte_cpu_to_be_16(state->dstPort);
      udpHdr->dgram_len = udpSize;
      udpHdr->dgram_cksum = 0;
  
      //
      // Prepare TxMessage Payload
      //
      TxMessage *payload = rte_pktmbuf_mtod_offset(mbuf[state->i], TxMessage*,
        sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr)+sizeof(rte_udp_hdr));
      payload->lcoreId  = state->lcoreId;
      payload->txqId    = state->txqId;
      payload->sequence = ++state->sequence;

      //
      // Finalize mbuf in DPDK
      //
      mbuf[state->i]->nb_segs = 1;
      mbuf[state->i]->pkt_len = packetSize;
      mbuf[state->i]->data_len = packetSize;
      mbuf[state->i]->ol_flags = (RTE_MBUF_F_TX_IPV4|RTE_MBUF_F_TX_IP_CKSUM);
      mbuf[state->i]->l2_len = sizeof(rte_ether_hdr);
      mbuf[state->i]->l3_len = sizeof(rte_ipv4_hdr);
      mbuf[state->i]->l4_len = sizeof(rte_udp_hdr);
      mbuf[state->i]->packet_type = (RTE_PTYPE_L2_ETHER|RTE_PTYPE_L3_IPV4|RTE_PTYPE_L4_UDP);

      if (++state->srcPort==0xfffe) {
        state->srcPort = 1024;
      }
      if (++state->dstPort==0xfffe) {
        state->dstPort = 1024;
      }
    }

    //
    // TX packets e.g. append to NIC's output queue to write to wire
    //
    state->sent = 0;
    state->left = TX_BURST_CAPACITY;
    while (state->left) {
      unsigned txCount = rte_eth_tx_burst(deviceId, txqIndex, mbuf+state->sent, state->left);
      state->left -= txCount;
      state->sent += txCount;
      state->count += txCount;
    }
  }

  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  uint64_t elapsedNs = timeDifference(start, now);

  double rateNsPerPacket = static_cast<double>(elapsedNs)/static_cast<double>(state->count);
  double pps = static_cast<double>(1000000000)/rateNsPerPacket;
  double bytesPerSecond = static_cast<double>(state->count)*static_cast<double>(packetSize)/
    (static_cast<double>(elapsedNs)/static_cast<double>(1000000000));
  double mbPerSecond = bytesPerSecond/static_cast<double>(1024)/static_cast<double>(1024);
  double payloadBytesPerSecond = static_cast<double>(state->count)*static_cast<double>(sizeof(TxMessage))/
    (static_cast<double>(elapsedNs)/static_cast<double>(1000000000));
  double payloadMbPerSecond = payloadBytesPerSecond/static_cast<double>(1024)/static_cast<double>(1024);

  printf("lcoreId: %02d, txqIndex: %02d: elsapsedNs: %lu, packetsQueued: %u, packetSizeBytes: %d, payloadSizeBytes: %lu, pps: %lf, nsPerPkt: %lf, bytesPerSec: %lf, mbPerSec: %lf, mbPerSecPayloadOnly: %lf\n", 
    id, txqIndex, elapsedNs, state->count, packetSize, sizeof(TxMessage), pps, rateNsPerPacket, bytesPerSecond, mbPerSecond, payloadMbPerSecond);

  return 0;
}

int serverMainLoop(int id, int rxqIndex, Reinvent::Dpdk::AWSEnaWorker *config) {
  const uint16_t deviceId = static_cast<uint16_t>(config->awsEnaConfig().deviceId());

  //
  // Finally the main point: receiving packets!
  //
  printf("lcoreId %02d rxqIndex %02d listening for packets\n", id, rxqIndex);
  
  unsigned count(0);
  std::vector<rte_mbuf*> mbuf(RX_BURST_CAPACITY);

  struct timespec start;
  clock_gettime(CLOCK_REALTIME, &start);

  while(!terminate) {
    //
    // Receive up to RX_BURST_CAPACITY packets
    //
    uint16_t rxCount = rte_eth_rx_burst(deviceId, rxqIndex, mbuf.data(), RX_BURST_CAPACITY);
    if (unlikely(rxCount==0)) {
      continue;
    }

    for (unsigned i=0; i<rxCount; ++i) {
      rte_pktmbuf_free(mbuf[i]);
    }

    if ((count += rxCount)>=REPORT_COUNT) {
      struct timespec now;
      clock_gettime(CLOCK_REALTIME, &now);
      uint64_t elapsedNs = timeDifference(start, now);

      const int packetSize = sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr)+sizeof(rte_udp_hdr)+sizeof(TxMessage);
      double rateNsPerPacket = static_cast<double>(elapsedNs)/static_cast<double>(count);
      double pps = static_cast<double>(1000000000)/rateNsPerPacket;
      double bytesPerSecond = static_cast<double>(count)*static_cast<double>(packetSize)/
        (static_cast<double>(elapsedNs)/static_cast<double>(1000000000));
      double mbPerSecond = bytesPerSecond/static_cast<double>(1024)/static_cast<double>(1024);
      double payloadBytesPerSecond = static_cast<double>(count)*static_cast<double>(sizeof(TxMessage))/
        (static_cast<double>(elapsedNs)/static_cast<double>(1000000000));
      double payloadMbPerSecond = payloadBytesPerSecond/static_cast<double>(1024)/static_cast<double>(1024);

      printf("lcoreId: %02d, rxqIndex: %02d: elsapsedNs: %lu, packetsQueued: %u, packetSizeBytes: %d, payloadSizeBytes: %lu, pps: %lf, nsPerPkt: %lf, bytesPerSec: %lf, mbPerSec: %lf, mbPerSecPayloadOnly: %lf\n", 
        id, rxqIndex, elapsedNs, count, packetSize, sizeof(TxMessage), pps, rateNsPerPacket, bytesPerSecond, mbPerSecond, payloadMbPerSecond);
    
      count = 0;
      clock_gettime(CLOCK_REALTIME, &start);
    }
  }

  return 0;
}

int clientEntryPoint(int id, int txqIndex, Reinvent::Dpdk::AWSEnaWorker *config) {
  assert(config);

  //
  // Get number of packets to send from client to server
  //
  int rc, tmp;
  std::string variable;
  Reinvent::Dpdk::Names::make(config->envPrefix(), &variable, "%s", "TXQ_PACKET_COUNT");
  if ((rc = config->env().valueAsInt(variable, &tmp, true, 1, 1000000000))!=0) {
    return rc;
  }
  unsigned packetCount = static_cast<unsigned>(tmp);
  
  //
  // Finally enter the main processing loop passing state collected here
  //
  return clientMainLoop(id, txqIndex, config, static_cast<unsigned>(packetCount));
}

int serverEntryPoint(int id, int rxqIndex, Reinvent::Dpdk::AWSEnaWorker *config) {
  assert(config);

  //
  // Go direct to main receiving loop
  //
  return serverMainLoop(id, rxqIndex, config);
}

int entryPoint(void *arg) {
  Reinvent::Dpdk::AWSEnaWorker *config = reinterpret_cast<Reinvent::Dpdk::AWSEnaWorker*>(arg);

  if (0==config) {
    REINVENT_UTIL_LOG_ERROR_VARGS("configuration pointer invalid: %p\n", arg);
    return -1;
  }

  int rc, id;
  bool tx, rx;
  int rxqIndex, txqIndex;

  //
  // What does this lcore do?
  //
  if ((rc = config->id(&id, &rx, &tx, &rxqIndex, &txqIndex))!=0) {
    return rc;
  } 
  
  rc = -1;
  if (tx) {
    rc = clientEntryPoint(id, txqIndex, config);
  } else if (rx) {
    rc = serverEntryPoint(id, rxqIndex, config);
  } else {
    REINVENT_UTIL_LOG_ERROR_VARGS("cannot classify DPDK lcore %d as RX or TX\n", id);
  }

  return rc;
}

int main(int argc, char **argv) {
  bool isServer(true);
  std::string prefix;
  std::string device("DPDK_NIC_DEVICE");

  parseCommandLine(argc, argv, &isServer, &prefix);

  Reinvent::Util::LogRuntime::resetEpoch();
  Reinvent::Util::LogRuntime::setSeverityLimit(REINVENT_UTIL_LOGGING_SEVERITY_TRACE);

  Reinvent::Util::Environment env;
  Reinvent::Dpdk::AWSEnaConfig config;

  //
  // Install signal handlers
  //
  signal(SIGINT, &handle_sig);
  signal(SIGTERM, &handle_sig);

  //
  // Initialize the AWS ENA Nic
  //
  int rc = Reinvent::Dpdk::InitAWS::enaUdp(device, prefix, &env, &config);
  REINVENT_UTIL_LOG_INFO(config << std::endl);
  if (rc!=0) {
    REINVENT_UTIL_LOG_FATAL_VARGS("Cannot initialize AWS ENA device rc=%d\n", rc);                                      
    onExit(config);
    return 1;
  }

  //
  // Setup and run AWSEnaWorkers. Now if the enviromment is setup right, the client
  // will only configure RXQ work (TX work turned off) and the server will only see
  // the configured RX work (TX work turned off). In turn that means the client only
  // will ever fall into 'txEntryPoint' and 'rxEntryPoint' for server. As such this
  // code file can be used for both modes. It's the enviroment 'prefix' that keeps
  // the code paths separate.
  {
    Reinvent::Dpdk::AWSEnaWorker worker(prefix, env, config);
    REINVENT_UTIL_LOG_INFO_VARGS("launching DPDK worker threads\n");
    if ((rc = rte_eal_mp_remote_launch(entryPoint, static_cast<void*>(&worker), CALL_MAIN))!=0) {
      REINVENT_UTIL_LOG_FATAL_VARGS("Cannot launch DPDK cores rc=%d\n", rc);
    } else {
      REINVENT_UTIL_LOG_INFO_VARGS("waiting for DPDK worker threads to stop\n");
      rte_eal_mp_wait_lcore();
    }
  }
  
  //
  // Cleanup and exit from main thread
  //
  onExit(config);

  return 0;
}
