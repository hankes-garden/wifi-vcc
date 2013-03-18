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
#include "ns3/udp-client-server-helper.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/propagation-delay-model.h"

// throughput evaluation

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("baseline");

const std::string SSID_DATA_PREFIX = "ssid_data_";

extern void PopulateArpCache();
extern void experiment(uint32_t nStaNum, uint32_t nUdpMaxPktCount, bool rts, bool hidden);

int main(int argc, char *argv[])
{

}

void experiment(uint32_t nStaNum, uint32_t nUdpMaxPktCount, bool rts, bool hidden)
{

	bool bLog = false;

	bool bEnableRTS = rts;
	bool bHasHiddenTerminal = hidden;

	uint32_t nSta = nStaNum;
	uint32_t nMaxPktCount = nUdpMaxPktCount;


	uint32_t nAp = 2;
	NS_ASSERT(nSta % 2 == 0 && nSta >= 4);

	uint32_t nFirstAP = nSta / 2;
	if (nFirstAP % 2 != 0)
	{
		nFirstAP++;
	}

	uint32_t nPort = 1987;

	uint32_t nMaxPktSize = 1024;
	Time interPktInterval = MicroSeconds(1500);

	uint32_t nRtsThresold =
			bEnableRTS ? (nMaxPktSize - 50) : (nMaxPktSize + 999);

	double simulationDuration = 100.0;
	double clientStart = 0.1;
	double clientDuration = 50.0;
	double svrStart = 0.1;
	double svrDuration = 50.0;

	if (bLog)
	{
		LogComponentEnableAll(LOG_LEVEL_WARN);

		LogComponentEnable("UdpServer", LOG_LEVEL_INFO);
		LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
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

	// divide nodes into 2 clusters
	NodeContainer staWithAp0;
	NodeContainer staWithAp1;
	for (uint32_t i = 0; i < staNodeContainer.GetN(); ++i)
	{
		if (i < nFirstAP)
		{
			staWithAp0.Add(staNodeContainer.Get(i));
		}
		else
		{
			staWithAp1.Add(staNodeContainer.Get(i));
		}
	}

	// setup mobility model - constant position & random Rectangle Position allocator
	MobilityHelper mobility;
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");

	// for STAs with first AP
	mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator", "X",
			StringValue("ns3::UniformRandomVariable[Min=0.0|Max=4.0]"), "Y",
			StringValue("ns3::UniformRandomVariable[Min=0.0|Max=4.0]"));
	mobility.Install(staWithAp0);

	// for STAs with second AP
	mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator", "X",
			StringValue("ns3::UniformRandomVariable[Min=6.0|Max=10.0]"), "Y",
			StringValue("ns3::UniformRandomVariable[Min=0.0|Max=4.0]"));
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

	// setup data rate control algorithm
	WifiHelper wifiHelper = WifiHelper::Default();
	wifiHelper.SetRemoteStationManager("ns3::ConstantRateWifiManager",
			"DataMode", StringValue("OfdmRate54Mbps"), "ControlMode",
			StringValue("OfdmRate54Mbps"), "RtsCtsThreshold",
			UintegerValue(nRtsThresold));

	// setup propagation loss model
	Ptr<PropagationLossModel> myLossModel = CreateObject<
			LogDistancePropagationLossModel>();

	Ptr<MatrixPropagationLossModel> matrixLoss = CreateObject<
			MatrixPropagationLossModel>();
	matrixLoss->SetDefaultLoss(0); // set default loss to 10 dB (no hidden terminal)
	if (bHasHiddenTerminal)
	{
		// add hidden terminal
		matrixLoss->SetLoss(staWithAp0.Get(1)->GetObject<MobilityModel>(),
				staWithAp1.Get(1)->GetObject<MobilityModel>(), 200);
		myLossModel->SetNext(matrixLoss);
	}

	// setup PHY & Channel
	Ptr<YansWifiChannel> wifiChannel = CreateObject<YansWifiChannel>();
	wifiChannel->SetPropagationLossModel(myLossModel);
	wifiChannel->SetPropagationDelayModel(
			CreateObject<ConstantSpeedPropagationDelayModel>());

	YansWifiPhyHelper phyHelper = YansWifiPhyHelper::Default();
	phyHelper.SetChannel(wifiChannel);

	// ------------Setup Mac------------------
	// setup STA MAC
	NqosWifiMacHelper macHelper = NqosWifiMacHelper::Default();

	// install netDevices: data first, ctrl later
	macHelper.SetType("ns3::StaWifiMac", "ActiveProbing", BooleanValue(false));
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	NetDeviceContainer staDataDevContainer = wifiHelper.Install(phyHelper,
			macHelper, staNodeContainer);

	// ------------AP------------------
	// install netDevices: data first, ctrl later
	macHelper.SetType("ns3::ApWifiMac");
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	NetDeviceContainer apDataDevContainer = wifiHelper.Install(phyHelper,
			macHelper, apNodeContainer);

	// -----------setup ssid & callbacks--------------------
	// setup ssid/callbacks for STA, and divide them into 2 clusters
	for (uint32_t i = 0; i < staNodeContainer.GetN(); ++i)
	{
		uint32_t ssid_number = 0;
		if (i < nFirstAP)
		{
			ssid_number = 0;
		}
		else
		{
			ssid_number = 1;
		}

		char strNumber[10] =
		{ 0 };
		std::sprintf(strNumber, "%d", ssid_number);

		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				staDataDevContainer.Get(i));
		Ptr<StaWifiMac> staDataMac = DynamicCast<StaWifiMac>(
				dataNetDevice->GetMac());

		// data ssid
		std::string strDataSsid = SSID_DATA_PREFIX + strNumber;
		staDataMac->SetSsid(Ssid(strDataSsid));

	}

	// setup ssid & callbacks for AP
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

	// setup Internet stack
	InternetStackHelper stack;
	stack.Install(apNodeContainer);
	stack.Install(staNodeContainer);

	// assign ip address, data first, ctrl later
	Ipv4AddressHelper address;
	address.SetBase("192.168.0.0", "255.255.255.0");
	Ipv4InterfaceContainer ipIfContainerApData = address.Assign(
			apDataDevContainer);
	Ipv4InterfaceContainer ipIfContainerStaData = address.Assign(
			staDataDevContainer);

	PopulateArpCache();

	// setup UDP server
	UdpServerHelper srvHelper(nPort);
	ApplicationContainer srvApp;
	for (uint32_t i = 0; i < staNodeContainer.GetN(); i = i + 2)
	{
		srvApp.Add(srvHelper.Install(staNodeContainer.Get(i)));
		std::cout << "install svr on STA_" << i << std::endl;
	}
	srvApp.Start(Seconds(svrStart));
	srvApp.Stop(Seconds(svrDuration));

	// setup UDP client
	ApplicationContainer clientApp;
	for (uint32_t i = 1; i < staNodeContainer.GetN(); i = i + 2)
	{
		UdpClientHelper clientHelper(ipIfContainerStaData.GetAddress(i - 1),
				nPort);
		clientHelper.SetAttribute("MaxPackets", UintegerValue(nMaxPktCount));
		clientHelper.SetAttribute("Interval", TimeValue(interPktInterval));
		clientHelper.SetAttribute("PacketSize", UintegerValue(nMaxPktSize));
		clientApp.Add(clientHelper.Install(staNodeContainer.Get(i)));

		std::cout << "install client on STA_" << i << ", bind to svr on STA_"
				<< i - 1 << std::endl;
	}
	clientApp.Start(Seconds(clientStart));
	clientApp.Stop(Seconds(clientDuration));

	Ipv4GlobalRoutingHelper::PopulateRoutingTables();

	// prepare the flow monitor
	FlowMonitorHelper flowmon;
	Ptr<FlowMonitor> monitor = flowmon.InstallAll();

	// run
	std::cout << "Start simulator." << std::endl;
	Simulator::Stop(Seconds(simulationDuration));
	Simulator::Run();

	std::cout << "Simulator ends: eva" << std::endl;

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
		std::cout << "data ip addr: " << ipIfContainerStaData.GetAddress(i)
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

	// evaluation
	monitor->CheckForLostPackets();

	Time lastPacketRxTime;
	double avgThroughput = 0.0;
	Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
			flowmon.GetClassifier());
	std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
	for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i =
			stats.begin(); i != stats.end(); ++i)
	{
//		Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);

		if (lastPacketRxTime < i->second.timeLastRxPacket)
		{
			lastPacketRxTime = i->second.timeLastRxPacket;
		}

		double th = i->second.rxBytes * 8.0
				/ (i->second.timeLastRxPacket.GetSeconds()
						- i->second.timeFirstTxPacket.GetSeconds())
				/ 1024 / 1024;

		std::cout << "Flow " << i->first /*<< " (" << t.sourceAddress << " -> "
		 << t.destinationAddress*/<< ")\n";
		std::cout << " Tx Bytes: " << i->second.txBytes << "\n";
		std::cout << " Rx Bytes: " << i->second.rxBytes << "\n";
		std::cout << " Tx packets: " << i->second.txPackets << "\n";
		std::cout << " Rx packets: " << i->second.rxPackets << "\n";
		std::cout << " Throughput: " << th<< " Mbps\n";

		avgThroughput += th ;
	}
	avgThroughput = avgThroughput / stats.size();
	std::cout << std::endl << "Average Throughput: " << avgThroughput << " Mbps"
				<< std::endl << std::endl;

	// channel utility
	Time avgIdleTime;
	for (uint32_t i = 0; i < staNodeContainer.GetN(); ++i)
	{
		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				staDataDevContainer.Get(i));
		Ptr<YansWifiPhy> dataPhy = DynamicCast<YansWifiPhy>(
				dataNetDevice->GetPhy());
		Time dataIdleTime = dataPhy->m_lastCumulateIdleTime;
//		Time lastRxTime = dataPhy->m_lastDataPacketRxTime;

		avgIdleTime += dataIdleTime;
//		std::cout << "STA_" << i << ":" << "dataCHN idle Time=" << dataIdleTime
//				<< ", lastRxTime=" << lastRxTime << std::endl;
	}

	for (uint32_t i = 0; i < apNodeContainer.GetN(); ++i)
	{
		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				apDataDevContainer.Get(i));
		Ptr<YansWifiPhy> dataPhy = DynamicCast<YansWifiPhy>(
				dataNetDevice->GetPhy());
		Time dataIdleTime = dataPhy->m_lastCumulateIdleTime;
//		Time lastRxTime = dataPhy->m_lastDataPacketRxTime;

		avgIdleTime += dataIdleTime;
//		std::cout << "AP_" << i << ":" << std::endl << "dataCHN idle Time="
//				<< dataIdleTime << ", lastRxTime=" << lastRxTime << std::endl;
	}

	avgIdleTime = Time(avgIdleTime.GetDouble() / (nSta + nAp));
//	std::cout << "Avg Idle Time = " << avgIdleTime << ", last Rx Time = "
//			<< lastPacketRxTime << std::endl;

	double percentage = avgIdleTime.GetDouble() / lastPacketRxTime.GetDouble();
	std::cout << "Channel idle percentage=" << percentage * 100 << "%"
			<< std::endl;

	Simulator::Destroy();


}

void PopulateArpCache()
{
	// Creates ARP Cache object
	Ptr<ArpCache> arp = CreateObject<ArpCache>();

	// Set ARP Timeout
	arp->SetAliveTimeout(Seconds(3600 * 24 * 365)); // 1-year

	// Populates ARP Cache with information from all nodes
	for (NodeList::Iterator i = NodeList::Begin(); i != NodeList::End(); ++i)
	{
		// Get an interactor to Ipv4L3Protocol instance
		Ptr<Ipv4L3Protocol> ip = (*i)->GetObject<Ipv4L3Protocol>();
		NS_ASSERT(ip !=0);

		// Get interfaces list from Ipv4L3Protocol iteractor
		ObjectVectorValue interfaces;
		ip->GetAttribute("InterfaceList", interfaces);

		// For each interface
		for (ObjectVectorValue::Iterator j = interfaces.Begin();
				j != interfaces.End(); j++)
		{
			// Get an interactor to Ipv4L3Protocol instance
			Ptr<Ipv4Interface> ipIface =
					(j->second)->GetObject<Ipv4Interface>();
			NS_ASSERT(ipIface != 0);

			// Get interfaces list from Ipv4L3Protocol iteractor
			Ptr<NetDevice> device = ipIface->GetDevice();
			NS_ASSERT(device != 0);

			// Get MacAddress assigned to this device
			Mac48Address addr = Mac48Address::ConvertFrom(device->GetAddress());

			// For each Ipv4Address in the list of Ipv4Addresses assign to this interface...
			for (uint32_t k = 0; k < ipIface->GetNAddresses(); k++)
			{
				// Get Ipv4Address
				Ipv4Address ipAddr = ipIface->GetAddress(k).GetLocal();

				// If Loopback address, go to the next
				if (ipAddr == Ipv4Address::GetLoopback())
					continue;

				// Creates an ARP entry for this Ipv4Address and adds it to the ARP Cache
				ArpCache::Entry * entry = arp->Add(ipAddr);
				entry->MarkWaitReply(0);
				entry->MarkAlive(addr);

				NS_LOG_ERROR ("Arp Cache: Adding the pair (" << addr << "," << ipAddr << ")");

			}
		}
	}

	// Assign ARP Cache to each interface of each node
	for (NodeList::Iterator i = NodeList::Begin(); i != NodeList::End(); ++i)
	{
		Ptr<Ipv4L3Protocol> ip = (*i)->GetObject<Ipv4L3Protocol>();
		NS_ASSERT(ip !=0);
		ObjectVectorValue interfaces;
		ip->GetAttribute("InterfaceList", interfaces);
		for (ObjectVectorValue::Iterator j = interfaces.Begin();
				j != interfaces.End(); j++)
		{
			Ptr<Ipv4Interface> ipIface =
					(j->second)->GetObject<Ipv4Interface>();
			ipIface->SetAttribute("ArpCache", PointerValue(arp));
		}
	}
}

