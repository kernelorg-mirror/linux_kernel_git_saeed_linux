.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

===============
Multi-PF Netdev
===============

Contents
========

- `Background`_
- `Overview`_
- `mlx5 implementation`_
- `Channels distribution`_
- `Topology`_
- `Steering`_
- `Mutually exclusive features`_

Background
==========

The advanced Multi-PF NIC technology enables several CPUs within a multi-socket server to
connect directly to the network, each through its own dedicated PCIe interface. Through either a
connection harness that splits the PCIe lanes between two cards or by bifurcating a PCIe slot for a
single card. This results in eliminating the network traffic traversing over the internal bus
between the sockets, significantly reducing overhead and latency, in addition to reducing CPU
utilization and increasing network throughput.

Overview
========

This feature adds support for combining multiple devices (PFs) of the same port in a Multi-PF
environment under one netdev instance. Passing traffic through different devices belonging to
different NUMA sockets saves cross-numa traffic and allows apps running on the same netdev from
different numas to still feel a sense of proximity to the device and achieve improved performance.

mlx5 implementation
===================

Multi-PF or Socket-direct in mlx5 is achieved by grouping PFs together which belong to the same
NIC and has the socket-direct property enabled, once all PFS are probed, we create a single netdev
to represent all of them, symmetrically, we destroy the netdev whenever any of the PFs is removed.

The netdev network channels are distributed between all devices, a proper configuration would utilize
the correct close numa node when working on a certain app/cpu.

We pick one PF to be a primary (leader), and it fills a special role. The other devices
(secondaries) are disconnected from the network at the chip level (set to silent mode). In silent
mode, no south <-> north traffic flowing directly through a secondary PF. It needs the assistance of
the leader PF (east <-> west traffic) to function. All RX/TX traffic is steered through the primary
to/from the secondaries.

Currently, we limit the support to PFs only, and up to two PFs (sockets).

Channels distribution
=====================

We distribute the channels between the different PFs to achieve local NUMA node performance
on multiple NUMA nodes.

Each combined channel works against one specific PF, creating all its datapath queues against it. We distribute
channels to PFs in a round-robin policy.

::

        Example for 2 PFs and 6 channels:
        +--------+--------+
        | ch idx | PF idx |
        +--------+--------+
        |    0   |    0   |
        |    1   |    1   |
        |    2   |    0   |
        |    3   |    1   |
        |    4   |    0   |
        |    5   |    1   |
        +--------+--------+


We prefer this round-robin distribution policy over another suggested intuitive distribution, in
which we first distribute one half of the channels to PF0 and then the second half to PF1.

The reason we prefer round-robin is, it is less influenced by changes in the number of channels. The
mapping between a channel index and a PF is fixed, no matter how many channels the user configures.
As the channel stats are persistent across channel's closure, changing the mapping every single time
would turn the accumulative stats less representing of the channel's history.

This is achieved by using the correct core device instance (mdev) in each channel, instead of them
all using the same instance under "priv->mdev".

Topology
========
Currently the sysfs is kept untouched, letting the netdev sysfs point to its primary PF.
Enhancing sysfs to reflect the actual topology is to be discussed and contributed separately.
For now, debugfs is being used to reflect the topology:

.. code-block:: bash

        $ grep -H . /sys/kernel/debug/mlx5/0000\:08\:00.0/sd/*
        /sys/kernel/debug/mlx5/0000:08:00.0/sd/group_id:0x00000101
        /sys/kernel/debug/mlx5/0000:08:00.0/sd/primary:0000:08:00.0 vhca 0x0
        /sys/kernel/debug/mlx5/0000:08:00.0/sd/secondary_0:0000:09:00.0 vhca 0x2

Steering
========
Secondary PFs are set to "silent" mode, meaning they are disconnected from the network.

In RX, the steering tables belong to the primary PF only, and it is its role to distribute incoming
traffic to other PFs, via cross-vhca steering capabilities. Nothing special about the RSS table
content, except that it needs a capable device to point to the receive queues of a different PF.

In TX, the primary PF creates a new TX flow table, which is aliased by the secondaries, so they can
go out to the network through it.

In addition, we set default XPS configuration that, based on the cpu, selects an SQ belonging to the
PF on the same node as the cpu.

XPS default config example:

NUMA node(s):          2
NUMA node0 CPU(s):     0-11
NUMA node1 CPU(s):     12-23

PF0 on node0, PF1 on node1.

- /sys/class/net/eth2/queues/tx-0/xps_cpus:000001
- /sys/class/net/eth2/queues/tx-1/xps_cpus:001000
- /sys/class/net/eth2/queues/tx-2/xps_cpus:000002
- /sys/class/net/eth2/queues/tx-3/xps_cpus:002000
- /sys/class/net/eth2/queues/tx-4/xps_cpus:000004
- /sys/class/net/eth2/queues/tx-5/xps_cpus:004000
- /sys/class/net/eth2/queues/tx-6/xps_cpus:000008
- /sys/class/net/eth2/queues/tx-7/xps_cpus:008000
- /sys/class/net/eth2/queues/tx-8/xps_cpus:000010
- /sys/class/net/eth2/queues/tx-9/xps_cpus:010000
- /sys/class/net/eth2/queues/tx-10/xps_cpus:000020
- /sys/class/net/eth2/queues/tx-11/xps_cpus:020000
- /sys/class/net/eth2/queues/tx-12/xps_cpus:000040
- /sys/class/net/eth2/queues/tx-13/xps_cpus:040000
- /sys/class/net/eth2/queues/tx-14/xps_cpus:000080
- /sys/class/net/eth2/queues/tx-15/xps_cpus:080000
- /sys/class/net/eth2/queues/tx-16/xps_cpus:000100
- /sys/class/net/eth2/queues/tx-17/xps_cpus:100000
- /sys/class/net/eth2/queues/tx-18/xps_cpus:000200
- /sys/class/net/eth2/queues/tx-19/xps_cpus:200000
- /sys/class/net/eth2/queues/tx-20/xps_cpus:000400
- /sys/class/net/eth2/queues/tx-21/xps_cpus:400000
- /sys/class/net/eth2/queues/tx-22/xps_cpus:000800
- /sys/class/net/eth2/queues/tx-23/xps_cpus:800000

Mutually exclusive features
===========================

The nature of Multi-PF, where different channels work with different PFs, conflicts with
stateful features where the state is maintained in one of the PFs.
For example, in the TLS device-offload feature, special context objects are created per connection
and maintained in the PF.  Transitioning between different RQs/SQs would break the feature. Hence,
we disable this combination for now.
