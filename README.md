# reinvent
Library: DPDK userspace library read/write UDP messages with congestion control

# Status
Early-alpha: code under heavy development. [Major limitations given here.](https://github.com/rodgarrison/reinvent/issues)

# Design Goals
This library intends to be a rewrite of [eRPC](https://github.com/erpc-io/eRPC) which was the motivation for this work.
[Refer to the eRPC techical paper for more information](https://www.usenix.org/system/files/nsdi19-kalia.pdf). eRPC 
(in its DPDK implementation) is a UDP only RPC library running over DPDK in userspace with congestion control to handle
UDP packet loss and flow control. The result is a highly efficient yet general purpose NIC I/O library.

# Current Reinvent features
* Decently documented
* Ships with a working IPV4 UDP TX/RX example
* Shows ENA checksum offload for IPV4, UDP checksums
* No-copy packet preparation
* DPDK configuration including UDP IPV4 routing information is defined outside code as enviromment variables ala
12-factor. For beginners it's far easier to understand configuration because readers are not dragged into DPDK code,
its structures, and working out what the code or config intends to accomplish
* Reinvent automatically configures lcores-to-HW-core assignment. Programmers set the desired lcore count in
environment variables. The library then works out RXQ/TXQ assignments from there.
* Reinvent provides a helper structure `AWSWorker` to make lcore startup straightforward
* Reinvent AWSConfig is streamable and is output in JSON format. Pipe into `python3 -m json.tool` to pretty print
* Reinvent provides a uniform structure to report errors: no ad hoc logging/assertions
* Tested on AWS `c5n` bare metal instances running AWS ENA NICs

# Getting Started
* [Read setup instructions](https://github.com/rodgarrison/reinvent/blob/main/doc/aws_ena_setup.md)
* [Read about DPDK packet design for IPV4 UDP](https://github.com/rodgarrison/reinvent/blob/main/doc/aws_ena_packet_design.md)

# Benchmarks
[A better comparison is based on Cloudfare work](https://blog.cloudflare.com/how-to-receive-a-million-packets/). This demonstrates UDP code sending 72 byte packets which includes 32 bytes of payload. The maximum I was able to achieve was around 1.1 million packets/sec:

```
# on sender machine:
$ sudo taskset -c 1,2 ./udpsender 172.31.68.106:432

# on receiver machine:
taskset -c 1,2,3 ./udpreceiver1 0.0.0.0:4321 3 1
  1.101M pps  33.596MiB / 281.820Mb 
  1.103M pps  33.662MiB / 282.376Mb
  1.102M pps  33.637MiB / 282.170Mb
  1.103M pps  33.647MiB / 282.249Mb
  1.104M pps  33.684MiB / 282.563Mb <--- 1.104*1000000*32/1024/1024=33.6MiB/sec, 1.104*1000000*32*8/1000/1000=282.6 million bits/sec 
```

All this work hits one queue on the RX side. However, that means DPDK is still an order of 10 slower than the Cloudfare kernel based work.

**Amazon advertises 100Gbps for `c5n.metal` instances if ENA is enabled**. Per Amazon:

```
This instance type supports the Elastic Fabric Adapter (EFA). EFAs support OS-bypass functionality, which enables High
Performance Computing (HPC) and Machine Learning (ML) applications to communicate directly with the network interface
hardware. EFAs provide lower latency and higher throughput than traditional TCP channels.
```

So perhaps there is hope. At this point in time, the second cut of performance testing may need to reflect improvements
in one or all of the following:

* bigger ring size
* a different TXQ burst size
* see if Amazon AWS ENA drivers will support RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE. Right now it does not
* better TXQ threshhold management. Right now config uses default which programs in 0 for all threshhold values
* force-set a different link speed. The code is not able to determine the link speed; both `rte_eth_link_get_nowait`. and `rte_eth_link_get` report `0x00` which does not correspond to `RTE_ETH_SPEED_NUM_` define not `RTE_ETH_SPEED_NUM_NONE`. After some testing via DPDK's `dpdk-testpmd` task, there seems to be no way to set or get a link speed. It seems unknowable and unsettable.
* need to determine if ncat is somehow using multiple TXQs. The DPDK test uses one. As per above, `ncat` uses one TXQ but much larger packet sizes.
