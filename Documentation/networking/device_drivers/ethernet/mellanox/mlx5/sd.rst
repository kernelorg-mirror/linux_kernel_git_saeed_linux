.. SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
.. include:: <isonum.txt>

==============================
Socket-Direct Netdev Combining
==============================

:Copyright: |copy| 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

Contents
========

- `Background`_
- `Overview`_
- `Channels distribution`_
- `Steering`_
- `Mutually exclusive features`_

Background
==========

NVIDIA Mellanox Socket Direct technology enables several CPUs within a multi-socket server to
connect directly to the network, each through its own dedicated PCIe interface. Through either a
connection harness that splits the PCIe lanes between two cards or by bifurcating a PCIe slot for a
single card. This results in eliminating the network traffic traversing over the internal bus
between the sockets, significantly reducing overhead and latency, in addition to reducing CPU
utilization and increasing network throughput.

Overview
========

This feature adds support for combining multiple devices (PFs) of the same port in a Socket Direct
environment under one netdev instance. Passing traffic through different devices belonging to
different NUMA sockets saves cross-numa traffic and allows apps running on the same netdev from
different numas to still feel a sense of proximity to the device and acheive improved performance.

We acheive this by grouping PFs together, and creating the netdev only once all group members are
probed. Symmetrically, we destroy the netdev once any of the PFs is removed.

The channels are distributed between all devices, a proper configuration would utilize the correct
close numa when working on a certain app/cpu.

We pick one device to be a primary (leader), and it fills a special role. The other devices
(secondaries) are disconnected from the network in the chip level (set to silent mode). All RX/TX
traffic is steered through the primary to/from the secondaries.

Currently, we limit the support to PFs only, and up to two devices (sockets).

Channels distribution
=====================

Distribute the channels between the different SD-devices to acheive local numa node performance on
multiple numas.

Each channel works against one specific mdev, creating all datapath queues against it. We distribute
channels to mdevs in a round-robin policy.

Example for 2 PFs and 6 channels:
+-------+-------+
| ch ix | PF ix |
+-------+-------+
|   0   |   0   |
|   1   |   1   |
|   2   |   0   |
|   3   |   1   |
|   4   |   0   |
|   5   |   1   |
+-------+-------+

This round-robin distribution policy is preferred over another suggested intuitive distribution, in
which we first distribute one half of the channels to PF0 and then the second half to PF1.

The reason we prefer round-robin is, it is less influenced by changes in the number of channels. The
mapping between a channel index and a PF is fixed, no matter how many channels the user configures.
As the channel stats are persistent to channels closure, changing the mapping every single time
would turn the accumulative stats less representing of the channel's history.

This is acheived by using the correct core device instance (mdev) in each channel, instead of them
all using the same instance under "priv->mdev".

Steering
========
Secondary PFs are set to "silent" mode, meaning they are disconnected from the network.

In RX, the steering tables belong to the primary PF only, and it is its role to distribute incoming
traffic to other PFs, via advanced HW cross-vhca steering capabilities.

In TX, the primary PF creates a new TX flow table, which is aliased by the secondaries, so they can
go out to the network through it.

In addition, we set default XPS configuration that, based on the cpu, selects an SQ belonging to the
PF on the same node as the cpu.

XPS default config example:

NUMA node(s):          2
NUMA node0 CPU(s):     0-11
NUMA node1 CPU(s):     12-23

PF0 on node0, PF1 on node1.

/sys/class/net/eth2/queues/tx-0/xps_cpus:000001
/sys/class/net/eth2/queues/tx-1/xps_cpus:001000
/sys/class/net/eth2/queues/tx-2/xps_cpus:000002
/sys/class/net/eth2/queues/tx-3/xps_cpus:002000
/sys/class/net/eth2/queues/tx-4/xps_cpus:000004
/sys/class/net/eth2/queues/tx-5/xps_cpus:004000
/sys/class/net/eth2/queues/tx-6/xps_cpus:000008
/sys/class/net/eth2/queues/tx-7/xps_cpus:008000
/sys/class/net/eth2/queues/tx-8/xps_cpus:000010
/sys/class/net/eth2/queues/tx-9/xps_cpus:010000
/sys/class/net/eth2/queues/tx-10/xps_cpus:000020
/sys/class/net/eth2/queues/tx-11/xps_cpus:020000
/sys/class/net/eth2/queues/tx-12/xps_cpus:000040
/sys/class/net/eth2/queues/tx-13/xps_cpus:040000
/sys/class/net/eth2/queues/tx-14/xps_cpus:000080
/sys/class/net/eth2/queues/tx-15/xps_cpus:080000
/sys/class/net/eth2/queues/tx-16/xps_cpus:000100
/sys/class/net/eth2/queues/tx-17/xps_cpus:100000
/sys/class/net/eth2/queues/tx-18/xps_cpus:000200
/sys/class/net/eth2/queues/tx-19/xps_cpus:200000
/sys/class/net/eth2/queues/tx-20/xps_cpus:000400
/sys/class/net/eth2/queues/tx-21/xps_cpus:400000
/sys/class/net/eth2/queues/tx-22/xps_cpus:000800
/sys/class/net/eth2/queues/tx-23/xps_cpus:800000

Mutually exclusive features
===========================

The nature of socket direct, where different channels work with different PFs, conflicts with
stateful features where the state is maintained in one of the PFs.
For exmaple, in the TLS device-offload feature, special context objects are created per connection
and maintained in the PF.  Transitioning between different RQs/SQs would break the feature. Hence,
we disable this combination for now.
