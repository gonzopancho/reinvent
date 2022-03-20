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
DPDK seems to deliver good results based on a first test. [A starting comparison is based on Cloudfare work](https://blog.cloudflare.com/how-to-receive-a-million-packets/). This demonstrates UDP code sending 72 byte packets which includes 32 bytes of payload **through the kernel**. The maximum I was able to achieve was around 1.1 million packets/sec. This work hits one queue on the RX side. 

```
# on sender machine running two cores:
$ sudo taskset -c 1,2 ./udpsender 172.31.68.106:432

# on receiver machine running 3 receive threads:
taskset -c 1,2,3 ./udpreceiver1 0.0.0.0:4321 3 1
  1.101M pps  33.596MiB / 281.820Mb 
  1.103M pps  33.662MiB / 282.376Mb
  1.102M pps  33.637MiB / 282.170Mb
  1.103M pps  33.647MiB / 282.249Mb
  1.104M pps  33.684MiB / 282.563Mb <--- 1.104*1000000*32/1024/1024=33.6MiB/sec, 1.104*1000000*32*8/1000/1000=282.6 million bits/sec 
```

Running the default `reinvent_dpdk_udp_integration_test.tsk` task with the default setup writing UDP packets with a payload size of 32 bytes on one core gives:

```
lcoreId: 00, txqIndex: 00: elsapsedNs: 450459947, packetsQueued: 1000005, packetSizeBytes: 74, payloadSizeBytes: 32, pps: 2219964.297958, nsPerPkt: 450.457695, bytesPerSec: 164277358.048883, mbPerSec: 156.667097, mbPerSecPayloadOnly: 67.747934
```

This is 2x the performance on half the number of running cores.
