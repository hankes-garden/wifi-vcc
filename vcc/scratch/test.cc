/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"

// Default Network Topology
//
//   Wifi 192.168.0.0
// cli--srv  cli--svr   AP
//  *    *    *    *    *
//  |    |    |    |    |
// n3   n2   n1   n0	ap_0

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("test");

int main(int argc, char *argv[])
{
	bool verbose = true;
	uint32_t nSta = 6;
	uint32_t nPort = 1987;

	if (verbose)
	{
		LogComponentEnable("UdpServer", LOG_LEVEL_INFO);
		LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
		LogComponentEnableAll(LOG_LEVEL_WARN);
	}
	LogComponentEnable("test", LOG_LEVEL_INFO);

	// create nodes
	NodeContainer wifiStaNodes;
	wifiStaNodes.Create(nSta);
	NodeContainer wifiApNode;
	wifiApNode.Create(1);

	// setup PHY & Channel
	YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
	YansWifiPhyHelper phyHelper = YansWifiPhyHelper::Default();
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	phyHelper.SetChannel(channel.Create());

	// setup data rate control algorithm
	WifiHelper wifiHelper = WifiHelper::Default();
	wifiHelper.SetRemoteStationManager("ns3::AarfWifiManager",
			"RtsCtsThreshold", UintegerValue(999900)); // do NOT need RTS

	// setup STA MAC
	NqosWifiMacHelper macHelper = NqosWifiMacHelper::Default();

	Ssid ssid = Ssid("test_ssid");
	macHelper.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid),
			"ActiveProbing", BooleanValue(false));

	NetDeviceContainer staDevices;
	staDevices = wifiHelper.Install(phyHelper, macHelper, wifiStaNodes);

	// setup AP MAC
	macHelper.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));

	NetDeviceContainer apDevices;
	apDevices = wifiHelper.Install(phyHelper, macHelper, wifiApNode);

	// setup mobility model
	MobilityHelper mobility;
	mobility.SetPositionAllocator("ns3::GridPositionAllocator", "MinX",
			DoubleValue(0.0), "MinY", DoubleValue(0.0), "DeltaX",
			DoubleValue(2.0), "DeltaY", DoubleValue(2.0), "GridWidth",
			UintegerValue(3), "LayoutType", StringValue("RowFirst"));
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

	mobility.Install(wifiStaNodes);
	mobility.Install(wifiApNode);

	// setup Internet stack
	InternetStackHelper stack;
	stack.Install(wifiApNode);
	stack.Install(wifiStaNodes);

	Ipv4AddressHelper address;
	address.SetBase("192.168.0.0", "255.255.255.0");
	address.Assign(apDevices);
	Ipv4InterfaceContainer wifiIpInterface = address.Assign(staDevices);

	// setup UDP server
	UdpServerHelper srvHelper(nPort);
	ApplicationContainer srvApp;
	for (uint32_t i = 0; i < wifiStaNodes.GetN(); i = i + 2)
	{
		srvApp.Add(srvHelper.Install(wifiStaNodes.Get(i)));
		std::cout << "install svr on STA_" << i << std::endl;
	}
	srvApp.Start(Seconds(1.0));
	srvApp.Stop(Seconds(60.0));

	// setup UDP client
	uint32_t nMaxPktSize = 1024;
	Time interPktInterval = MicroSeconds(1000);
	uint32_t nMaxPktCount = 5000;

	ApplicationContainer clientApp;
	for (uint32_t i = 1; i < wifiStaNodes.GetN(); i = i + 2)
	{
		UdpClientHelper clientHelper(wifiIpInterface.GetAddress(i - 1), nPort);
		clientHelper.SetAttribute("MaxPackets", UintegerValue(nMaxPktCount));
		clientHelper.SetAttribute("Interval", TimeValue(interPktInterval));
		clientHelper.SetAttribute("PacketSize", UintegerValue(nMaxPktSize));
		clientApp.Add(clientHelper.Install(wifiStaNodes.Get(i)));

		std::cout << "install client on STA_" << i << ", bound to svr on STA_"
				<< i - 1 << std::endl;
	}
	clientApp.Start(Seconds(5.0));
	clientApp.Stop(Seconds(50.0));

	// prepare the flow monitor
	FlowMonitorHelper flowmon;
	Ptr<FlowMonitor> monitor = flowmon.InstallAll();

	// STA information
	for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
	{
		Ptr<Node> staNode = wifiStaNodes.Get(i);

		std::cout << "STA_" << i << ":" << std::endl;

		// mac addr
		std::cout << "NetDevice addr: "
				<< DynamicCast<WifiNetDevice>(staNode->GetDevice(0))->GetMac()->GetAddress()
				<< std::endl;

		// position
		Ptr<MobilityModel> position = staNode->GetObject<MobilityModel>();
		NS_ASSERT(position != 0);
		Vector pos = position->GetPosition();
		std::cout << "Position: x=" << pos.x << ", y=" << pos.y << ", z="
				<< pos.z << std::endl;

		std::cout << std::endl;
	}

	// AP information
	for (uint32_t i = 0; i < wifiApNode.GetN(); ++i)
	{
		Ptr<Node> apNode = wifiApNode.Get(i);

		std::cout << "AP_" << i << ":" << std::endl;

		// mac addr
		std::cout << "NetDevice addr: "
				<< DynamicCast<WifiNetDevice>(apNode->GetDevice(0))->GetMac()->GetAddress()
				<< std::endl;

		// position
		Ptr<MobilityModel> position = apNode->GetObject<MobilityModel>();
		NS_ASSERT(position != 0);
		Vector pos = position->GetPosition();
		std::cout << "Position: x=" << pos.x << ", y=" << pos.y << ", z="
				<< pos.z << std::endl;

		std::cout << std::endl;
	}

	// run
	NS_LOG_INFO("Start simulator.");
	Simulator::Stop(Seconds(100.0));
	Simulator::Run();

	NS_LOG_INFO("Simulator ends: test");

	// evaluation
	monitor->CheckForLostPackets();

	Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
			flowmon.GetClassifier());
	std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
	for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i =
			stats.begin(); i != stats.end(); ++i)
	{
		Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

		std::cout << "Flow " << i->first << " (" << t.sourceAddress << " -> "
				<< t.destinationAddress << ")\n";
		std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
		std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
		std::cout << "  Tx packets:   " << i->second.txPackets << "\n";
		std::cout << "  Rx packets:   " << i->second.rxPackets << "\n";
		std::cout << "  Throughput: "
				<< i->second.rxBytes * 8.0
						/ (i->second.timeLastRxPacket.GetSeconds()
								- i->second.timeFirstTxPacket.GetSeconds())
						/ 1024 / 1024 << " Mbps\n";
	}

	Simulator::Destroy();

	return 0;
}
