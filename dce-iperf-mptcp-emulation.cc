/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010-12 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Modified version of dce-iperf.cc & dce-iperf-mptcp.cc that supports emulation
 */

// ===========================================================================
//
// This is similar to dce-iperf.cc where both nodes are ns-3 nodes,
// but in this example, if the --emulation argument is set to true,
// node 1 will be a Linux container.  See the HOWTO on the ns-3
// wiki:  http://www.nsnam.org/wiki/HOWTO_use_CORE_to_test_ns-3_TCP
//
// the argument "--emulation" can be used to enable emulation
// the argument "--clientNode" can be used to set the client
// node 0
//
// The program runs for 30 seconds
//
//         node 0 (ns-3)                   node 1 (container)
//   +----------------+                   +----------------+
//   | dce-iperf-mptcp|                   |     iperf      |
//   +----------------+                   +----------------+
//   |    10.0.0.1    |                   |    10.0.0.2    |
//   +----------------+                   +----------------+
//   |  FdNetDevice   |                   |    Ethernet    |
//   +----------------+                   +----------------+
//           |                                      |
//           +-------- <Real Ethernet> -------------+
//
// 2 nodes : iperf client and iperf server ....
//
// Note : Tested with iperf 2.0.5, you need to modify iperf source in order to
//        allow DCE to have a chance to end an endless loop in iperf as follow:
//        in source named Thread.c at line 412 in method named thread_rest
//        you must add a sleep (1); to break the infinite loop....
// ===========================================================================

#include "ns3/network-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/dce-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/constant-position-mobility-model.h"
// Picked from dce-iperf-emulation.cc
#include "ns3/csma-module.h"
#include "ns3/fd-net-device-module.h"
#include "ccnx/misc-tools.h"

// Initialize namespace and logging
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DceIperfMptcpEmulation");

int main (int argc, char *argv[])
{

  // Provide defaults
  std::string stack = "ns3";
  bool useUdp = 0;
  std::string bandWidth = "1m";
  bool useEmulation = 0;
  uint32_t clientNode = 0;

  // Some better defaults for high bandwidth-delay product testing
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (640000));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (640000));
  Config::SetDefault ("ns3::TcpSocket::InitialSlowStartThreshold", UintegerValue (0xfffff));

  CommandLine cmd;
  cmd.AddValue ("stack", "Name of DCE IP stack: ns3/linux/freebsd. Default:", stack);
  cmd.AddValue ("udp", "Use UDP.  Default: ", useUdp);
  cmd.AddValue ("bw", "UDP bandwidth limit. Default: ", bandWidth);
  cmd.AddValue ("emulation", "Use emulation. Default: ", useEmulation);
  cmd.AddValue ("clientNode", "Client node (0 or 1).  Default: ", clientNode);
  cmd.Parse (argc, argv);

  if (clientNode > 0)
    {
      NS_FATAL_ERROR ("Only legal values are 0");
    }

  if (useEmulation)
    {
      GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));

      GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    }

  NodeContainer nodes;
  EmuFdNetDeviceHelper emu;
  NetDeviceContainer devices;
  CsmaHelper csma;

  if (useEmulation)
    {
      nodes.Create (1);
      std::string deviceName ("eth0");
      emu.SetDeviceName (deviceName);
      devices = emu.Install (nodes.Get (0));
      Ptr<NetDevice> device = devices.Get (0);
      device->SetAttribute ("Address", Mac48AddressValue (Mac48Address::Allocate ()));
    }
  else 
    {
      NS_FATAL_ERROR ("Emulate ClientNode0 Only");
    }

  DceManagerHelper dceManager;
  dceManager.SetTaskManagerAttribute ("FiberManagerType", StringValue ("UcontextFiberManager"));

  if (stack == "ns3")
    {
      InternetStackHelper stack;
      stack.Install (nodes);
      dceManager.Install (nodes);
    }
  else if (stack == "linux")
    {
#ifdef KERNEL_STACK
      dceManager.SetNetworkStack ("ns3::LinuxSocketFdFactory", "Library", StringValue ("liblinux.so"));
      dceManager.Install (nodes);
      LinuxStackHelper stack;
      stack.Install (nodes);
#else
      NS_LOG_ERROR ("Linux kernel stack for DCE is not available. build with dce-linux module.");
      // silently exit
      return 0;
#endif
    }
  else if (stack == "freebsd")
    {
#ifdef KERNEL_STACK
      dceManager.SetNetworkStack ("ns3::FreeBSDSocketFdFactory", "Library", StringValue ("libfreebsd.so"));
      dceManager.Install (nodes);
      FreeBSDStackHelper stack;
      stack.Install (nodes);
#else
      NS_LOG_ERROR ("FreeBSD kernel stack for DCE is not available. build with dce-freebsd module.");
      // silently exit
      return 0;
#endif
    }

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.252");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

#ifdef KERNEL_STACK
  if (stack == "linux")
    {
      LinuxStackHelper::PopulateRoutingTables ();
    }
#endif

  // Schedule Up/Down (XXX: didn't work...)
  LinuxStackHelper::RunIp (nodes.Get (0), Seconds (1.0), "link set dev eth0 multipath off");
  LinuxStackHelper::RunIp (nodes.Get (0), Seconds (15.0), "link set dev eth0 multipath on");
  LinuxStackHelper::RunIp (nodes.Get (0), Seconds (30.0), "link set dev eth0 multipath off");

  // debug
  stack.SysctlSet (nodes, ".net.mptcp.mptcp_debug", "1");

  DceApplicationHelper dce;
  ApplicationContainer apps;

  dce.SetStackSize (1 << 20);

  dce.SetBinary ("iperf");
  dce.ResetArguments ();
  dce.ResetEnvironment ();
  // Launch iperf client on node 0
  dce.AddArgument ("-c");
  dce.AddArgument ("10.0.0.2");
  dce.AddArgument ("-i");
  dce.AddArgument ("1");
  dce.AddArgument ("--time");
  dce.AddArgument ("10");
  if (useUdp)
    {
      dce.AddArgument ("-u");
      dce.AddArgument ("-b");
      dce.AddArgument (bandWidth);
    }

  apps = dce.Install (nodes.Get (0));
  apps.Start (Seconds (0.7));
  apps.Stop (Seconds (30));

  if (useEmulation)
    {
      emu.EnablePcap ("iperf-mptcp-emulation-" + stack, devices.Get(0), true);
    }

  Simulator::Stop (Seconds (30.1));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}