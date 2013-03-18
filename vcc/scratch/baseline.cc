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
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/udp-client-server-helper.h"

//baseline throughput
//NOTE:
//     1. disable the RTS/CTS

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("eva");

const std::string SSID_DATA_PREFIX = "ssid_data_";

int main(int argc, char *argv[])
{
	bool verbose = true;
	uint32_t nSta = 4;
	NS_ASSERT(nSta % 2 == 0);
	uint32_t nAp = 2;

	uint32_t nFirstAP = nSta / 2;
	if (nFirstAP % 2 != 0)
	{
		nFirstAP++;
	}

	uint32_t nPort = 1987;

	uint32_t nMaxPktSize = 1024;
	Time interPktInterval = MicroSeconds(1500);
	uint32_t nMaxPktCount = 1000;

	if (verbose)
	{
		LogComponentEnableAll(LOG_LEVEL_WARN);

		LogComponentEnable("LinUdpServer", LOG_LEVEL_INFO);
		LogComponentEnable("LinUdpClient", LOG_LEVEL_INFO);
		LogComponentEnable("eva", LOG_LEVEL_INFO);

	}
	else
	{
		LogComponentEnableAll(LOG_NONE);
	}

	// create nodes
	NodeContainer staNodeContainer;
	staNodeContainer.Create(nSta);

	NodeContainer apNodeContainer;
	apNodeContainer.Create(nAp);

	// setup data rate control algorithm
	WifiHelper wifiHelper = WifiHelper::Default();
	wifiHelper.SetRemoteStationManager("ns3::AarfWifiManager",
			"RtsCtsThreshold", UintegerValue(nMaxPktSize + 1000)); // disable RTS & CTS
	wifiHelper.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
	"ControlMode", StringValue ("OfdmRate54Mbps"));
	wifiHelper.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
	                                "DataMode", StringValue ("OfdmRate54Mbps"));

	// setup PHY & Channel
	YansWifiChannelHelper chnHelper = YansWifiChannelHelper::Default();
	YansWifiPhyHelper phyHelper = YansWifiPhyHelper::Default();
	phyHelper.SetChannel(chnHelper.Create());

	// ------------Setup Mac------------------
	// setup STA MAC
	NqosWifiMacHelper macHelper = NqosWifiMacHelper::Default();

	// install netDevices: data first, ctrl later
	macHelper.SetType("ns3::StaWifiMac", "ActiveProbing",
			BooleanValue(false));
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	NetDeviceContainer staDataDevContainer = wifiHelper.Install(phyHelper,
			macHelper, staNodeContainer);


	// ------------AP------------------
	// install netDevices: data first, ctrl later
	macHelper.SetType("ns3::ApWifiMac");
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	NetDeviceContainer apDataDevContainer = wifiHelper.Install(phyHelper,
			macHelper, apNodeContainer);

	// -----------setup ssid --------------------

	// setup ssid/callbacks for STA, and divide them into 2 clusters
	NodeContainer staWithAp0;
	NodeContainer staWithAp1;
	for (uint32_t i = 0; i < staNodeContainer.GetN(); ++i)
	{
		uint32_t ssid_number = 0;
		if (i < nFirstAP)
		{
			ssid_number = 0;
			staWithAp0.Add(staNodeContainer.Get(i));
		}
		else
		{
			ssid_number = 1;
			staWithAp1.Add(staNodeContainer.Get(i));
		}

		char strNumber[10] = { 0 };
		std::sprintf(strNumber, "%d", ssid_number);

		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				staDataDevContainer.Get(i));
		Ptr<StaWifiMac> staDataMac = DynamicCast<StaWifiMac>(
				dataNetDevice->GetMac());

		// data ssid
		std::string strDataSsid = SSID_DATA_PREFIX + strNumber;
		staDataMac->SetSsid(Ssid(strDataSsid));

	}

	// setup ssid for AP
	for (uint32_t i = 0; i < apNodeContainer.GetN(); ++i)
	{
		Ptr<WifiNetDevice> apDataNetDevice = DynamicCast<WifiNetDevice>(
				apDataDevContainer.Get(i));
		Ptr<ApWifiMac> apDataMac = DynamicCast<ApWifiMac>(
				apDataNetDevice->GetMac());

		// data ssid
		char strNumber[20] =
		{ 0 };
		std::sprintf(strNumber, "%d", i);
		std::string strDataSsid = SSID_DATA_PREFIX + strNumber;
		apDataMac->SetSsid(Ssid(strDataSsid));

	}

	// setup mobility model - constant position & random Rectangle Position allocator
	MobilityHelper mobility;
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

	// for STAs with first AP
	mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator", "X",
			StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=4.0]"), "Y",
			StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=4.0]"));
	mobility.Install(staWithAp0);

	// for STAs with second AP
	mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator", "X",
			StringValue ("ns3::UniformRandomVariable[Min=6.0|Max=10.0]"), "Y",
			StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=4.0]"));
	mobility.Install(staWithAp1);

	// for APs
	mobility.Install(apNodeContainer);
	// AP information
	for (uint32_t i = 0; i < apNodeContainer.GetN(); ++i)
	{
		Ptr<Node> apNode = apNodeContainer.Get(i);

		// position
		Ptr<MobilityModel> position = apNode->GetObject<MobilityModel>();
		NS_ASSERT(position != 0);
		Vector newPos;
		if (i == 0)
		{
			newPos.x = 2;
			newPos.y = 2;
			newPos.z = 0;
		}
		else if (i == 1)
		{
			newPos.x = 8;
			newPos.y = 2;
			newPos.z = 0;
		}
		else
		{
			NS_ASSERT_MSG(false, "Only allow 2 APs");
		}

		position->SetPosition(newPos);
	}

	// setup Internet stack
	InternetStackHelper stack;
	stack.Install(apNodeContainer);
	stack.Install(staNodeContainer);

	// assign ip address, data first, ctrl later
	Ipv4AddressHelper address;
	address.SetBase("192.168.0.0", "255.255.255.0");
	address.Assign(apDataDevContainer);
	Ipv4InterfaceContainer ipInterfaceContainerData = address.Assign(
			staDataDevContainer);

	// setup UDP server
	UdpServerHelper srvHelper(nPort);
	ApplicationContainer srvApp;
	for (uint32_t i = 0; i < staNodeContainer.GetN(); i = i + 2)
	{
		srvApp.Add(
				srvHelper.Install(staNodeContainer.Get(i) ) );
		std::cout << "install svr on STA_" << i << std::endl;
	}
	srvApp.Start(Seconds(10.0));
	srvApp.Stop(Seconds(60.0));

	// setup UDP client
	ApplicationContainer clientApp;
	for (uint32_t i = 1; i < staNodeContainer.GetN(); i = i + 2)
	{
		UdpClientHelper clientHelper(
				ipInterfaceContainerData.GetAddress(i - 1), nPort);
		clientHelper.SetAttribute("MaxPackets", UintegerValue(nMaxPktCount));
		clientHelper.SetAttribute("Interval", TimeValue(interPktInterval));
		clientHelper.SetAttribute("PacketSize", UintegerValue(nMaxPktSize));
		clientApp.Add(
				clientHelper.Install(staNodeContainer.Get(i) ) );

		std::cout << "install client on STA_" << i << ", bind to svr on STA_"
				<< i - 1 << std::endl;
	}
	clientApp.Start(Seconds(15.0));
	clientApp.Stop(Seconds(50.0));

	Ipv4GlobalRoutingHelper::PopulateRoutingTables();

	// prepare the flow monitor
	FlowMonitorHelper flowmon;
	Ptr<FlowMonitor> monitor = flowmon.InstallAll();

	// print out topology information-----------------------
	std::cout << std::endl;
	std::cout << "-------------Topology information----------" << std::endl;
	// STA information
	for (uint32_t i = 0; i < staNodeContainer.GetN(); ++i)
	{
		Ptr<Node> staNode = staNodeContainer.Get(i);

		std::cout << "STA_" << i << ":" << std::endl;

		// mac addr
		std::cout << "data NetDevice addr: "
				<< DynamicCast<WifiNetDevice>(staNode->GetDevice(0))->GetMac()->GetAddress()
				<< std::endl;
		std::cout << "data ssid: "
				<< DynamicCast<WifiNetDevice>(staNode->GetDevice(0))->GetMac()->GetSsid()
				<< std::endl;
		std::cout << "data ip addr: " << ipInterfaceContainerData.GetAddress(i)
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
	for (uint32_t i = 0; i < apNodeContainer.GetN(); ++i)
	{
		Ptr<Node> apNode = apNodeContainer.Get(i);

		std::cout << "AP_" << i << ":" << std::endl;

		// mac addr
		std::cout << "data NetDevice addr: "
				<< DynamicCast<WifiNetDevice>(apNode->GetDevice(0))->GetMac()->GetAddress()
				<< std::endl;
		std::cout << "data ssid: "
				<< DynamicCast<WifiNetDevice>(apNode->GetDevice(0))->GetMac()->GetSsid()
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
	std::cout << "Start simulator." << std::endl;
	Simulator::Stop(Seconds(100.0));
	Simulator::Run();

	std::cout << "Simulator ends: eva" << std::endl;

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
