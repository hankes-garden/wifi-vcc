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
#include "ns3/lin-udp-client-server-helper.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/propagation-delay-model.h"

#define VALID_FLOW_COUNT 0

// throughput evaluation

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("eva");

const std::string SSID_DATA_PREFIX = "ssid_data_";
const std::string SSID_CTRL_PREFIX = "ssid_ctrl_";

extern void PopulateArpCache();
extern void vcc(uint32_t nStaNum, uint32_t nUdpMaxPktCount,
		uint32_t nUdpPktSize, bool rts, bool hidden);
extern void baseline(uint32_t nStaNum, uint32_t nUdpMaxPktCount,
		uint32_t nUdpPktSize, bool rts, bool hidden);

int main(int argc, char *argv[])
{
//	bool bEnableRTS = true;
	bool bHasHiddenTerminal = true;

	uint32_t nSta = 22;
	uint32_t nMaxPktCount = 190;
	uint32_t nPktSize = 1500;

//	std::cout << "===== vcc ==========================" << std::endl;
//		vcc(nSta, nMaxPktCount, nPktSize, bEnableRTS, bHasHiddenTerminal);
//		std::cout << std::endl;

	std::cout << "===== baseline_rts =================" << std::endl;
	baseline(nSta, nMaxPktCount, nPktSize, true, bHasHiddenTerminal);
	std::cout << std::endl;

	std::cout << "===== baseline_no_rts ==============" << std::endl;
	baseline(nSta, nMaxPktCount, nPktSize, false, bHasHiddenTerminal);
	std::cout << std::endl;



	return 0;

}

void vcc(uint32_t nStaNum, uint32_t nUdpMaxPktCount, uint32_t nUdpPktSize,
		bool rts, bool hidden)
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
	bool g_bGlobalFirstRts = true;
	uint32_t nMaxCwForCtrlChn = 15;

	uint32_t nMaxPktSize = nUdpPktSize;
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

		LogComponentEnable("LinUdpServer", LOG_LEVEL_INFO);
		LogComponentEnable("LinUdpClient", LOG_LEVEL_INFO);
		LogComponentEnable("eva", LOG_LEVEL_INFO);
//		LogComponentEnable("DcfManager", LOG_LEVEL_DEBUG);

	}
	else
	{

		LogComponentEnableAll(LOG_NONE);
	}

//	LogComponentEnable("ArpL3Protocol", LOG_LEVEL_LOGIC);

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
	macHelper.SetType("ns3::StaDataWifiMac", "ActiveProbing",
			BooleanValue(false));
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	NetDeviceContainer staDataDevContainer = wifiHelper.Install(phyHelper,
			macHelper, staNodeContainer);

	macHelper.SetType("ns3::StaCtrlWifiMac", "ActiveProbing",
			BooleanValue(false));
	phyHelper.Set("ChannelNumber", UintegerValue(6));
	NetDeviceContainer staCtrlDevContainer = wifiHelper.Install(phyHelper,
			macHelper, staNodeContainer);

	// ------------AP------------------
	// install netDevices: data first, ctrl later
	macHelper.SetType("ns3::ApWifiMacData");
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	NetDeviceContainer apDataDevContainer = wifiHelper.Install(phyHelper,
			macHelper, apNodeContainer);

	macHelper.SetType("ns3::ApWifiMacCtrl", "BeaconInterval",
			TimeValue(Seconds(100)));
	phyHelper.Set("ChannelNumber", UintegerValue(6));
	NetDeviceContainer apCtrlDevContainer = wifiHelper.Install(phyHelper,
			macHelper, apNodeContainer);

	// -----------setup ssid & callbacks--------------------
	// NOTE: this MUST be done after all nodes have finished the device installation
	NodeContainer allNodes = staNodeContainer;
	allNodes.Add(apNodeContainer);

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
		Ptr<StaDataWifiMac> staDataMac = DynamicCast<StaDataWifiMac>(
				dataNetDevice->GetMac());
		Ptr<DataDcaTxop> staDataDca = staDataMac->m_dca;

		Ptr<WifiNetDevice> staCtrlNetDevice = DynamicCast<WifiNetDevice>(
				staCtrlDevContainer.Get(i));
		Ptr<StaCtrlWifiMac> staCtrlMac = DynamicCast<StaCtrlWifiMac>(
				staCtrlNetDevice->GetMac());
		Ptr<CtrlDcaTxop> staCtrlDca = staCtrlMac->m_dca;

		// set maxCw for ctrl channel
		staCtrlDca->SetMaxCw(nMaxCwForCtrlChn);

		// data ssid
		std::string strDataSsid = SSID_DATA_PREFIX + strNumber;
		staDataMac->SetSsid(Ssid(strDataSsid));

		// ctrl ssid
		std::string strCtrlSsid = SSID_CTRL_PREFIX + strNumber;
		staCtrlMac->SetSsid(Ssid(strCtrlSsid));

		// set callback for dataDcaTxop
		staDataDca->SetSendByCtrlChannleCallback(
				MakeCallback(&CtrlDcaTxop::SendByCtrlChannelImpl, staCtrlDca));

		// set global information for dataDcaTxop
		staDataDca->SetGlobalRtsSignal(&g_bGlobalFirstRts);

		// set topology to dataDcaTxop
		staDataDca->SetNodeContainer(&allNodes);

		// set topology to ctrlDcaTxop
		staCtrlDca->SetNodeContainer(&allNodes);

		// set callback for ctrlDcaTxop
		staCtrlDca->SetNotifyDataChannelCallback(
				MakeCallback(&DataDcaTxop::NotifyDataChannelImpl, staDataDca));

	}

	// setup ssid & callbacks for AP
	for (uint32_t i = 0; i < apNodeContainer.GetN(); ++i)
	{
		Ptr<WifiNetDevice> apDataNetDevice = DynamicCast<WifiNetDevice>(
				apDataDevContainer.Get(i));
		Ptr<ApWifiMacData> apDataMac = DynamicCast<ApWifiMacData>(
				apDataNetDevice->GetMac());
		Ptr<DataDcaTxop> apDataDca = apDataMac->m_dca;

		Ptr<WifiNetDevice> apCtrlNetDevice = DynamicCast<WifiNetDevice>(
				apCtrlDevContainer.Get(i));
		Ptr<ApWifiMacCtrl> apCtrlMac = DynamicCast<ApWifiMacCtrl>(
				apCtrlNetDevice->GetMac());
		Ptr<CtrlDcaTxop> apCtrlDca = apCtrlMac->m_dca;

		// set maxCw for ctrl channel
		apCtrlDca->SetMaxCw(nMaxCwForCtrlChn);

		// data ssid
		char strNumber[20] =
		{ 0 };
		std::sprintf(strNumber, "%d", i);
		std::string strDataSsid = SSID_DATA_PREFIX + strNumber;
		apDataMac->SetSsid(Ssid(strDataSsid));

		// ctrl ssid
		std::string strCtrlSsid = SSID_CTRL_PREFIX + strNumber;
		apCtrlMac->SetSsid(Ssid(strCtrlSsid));

		// set callback for dataDcaTxop
		apDataDca->SetSendByCtrlChannleCallback(
				MakeCallback(&CtrlDcaTxop::SendByCtrlChannelImpl, apCtrlDca));

		// set global information for dataDcaTxop
		apDataDca->SetGlobalRtsSignal(&g_bGlobalFirstRts);

		// set topology to dataDcaTxop
		apDataDca->SetNodeContainer(&allNodes);

		// set topology to ctrlDcaTxop
		apCtrlDca->SetNodeContainer(&allNodes);

		// set callback for ctrlDcaTxop
		apCtrlDca->SetNotifyDataChannelCallback(
				MakeCallback(&DataDcaTxop::NotifyDataChannelImpl, apDataDca));

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

//	ConfigStaticArp(staNodeContainer, apNodeContainer, ipIfContainerStaData, ipIfContainerApData);
	PopulateArpCache();

	// setup UDP server
	LinUdpServerHelper srvHelper(nPort);
	ApplicationContainer srvApp;
	for (uint32_t i = 0; i < staNodeContainer.GetN(); i = i + 2)
	{
		srvApp.Add(
				srvHelper.Install(staNodeContainer.Get(i),
						staDataDevContainer.Get(i)));
		NS_LOG_ERROR("install svr on STA_" << i );
	}
	srvApp.Start(Seconds(svrStart));
	srvApp.Stop(Seconds(svrDuration));

	// setup UDP client
	ApplicationContainer clientApp;
	for (uint32_t i = 1; i < staNodeContainer.GetN(); i = i + 2)
	{
		LinUdpClientHelper clientHelper(ipIfContainerStaData.GetAddress(i - 1),
				nPort);
		clientHelper.SetAttribute("MaxPackets", UintegerValue(nMaxPktCount));
		clientHelper.SetAttribute("Interval", TimeValue(interPktInterval));
		clientHelper.SetAttribute("PacketSize", UintegerValue(nMaxPktSize));
		clientApp.Add(
				clientHelper.Install(staNodeContainer.Get(i),
						staDataDevContainer.Get(i)));

		NS_LOG_ERROR("install client on STA_" << i << ", bind to svr on STA_"
				<< i - 1 );
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

	if (bLog)
	{
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
			std::cout << "ctrl NetDevice addr: "
					<< DynamicCast<WifiNetDevice>(staNode->GetDevice(1))->GetMac()->GetAddress()
					<< std::endl;
			std::cout << "ctrl ssid: "
					<< DynamicCast<WifiNetDevice>(staNode->GetDevice(1))->GetMac()->GetSsid()
					<< std::endl;
			//		std::cout << "ctrl ip addr: " << ipInterfaceContainerCtrl.GetAddress(i)
			//				<< std::endl;

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
			std::cout << "ctrl NetDevice addr: "
					<< DynamicCast<WifiNetDevice>(apNode->GetDevice(1))->GetMac()->GetAddress()
					<< std::endl;
			std::cout << "ctrl ssid: "
					<< DynamicCast<WifiNetDevice>(apNode->GetDevice(1))->GetMac()->GetSsid()
					<< std::endl;

			// position
			Ptr<MobilityModel> position = apNode->GetObject<MobilityModel>();
			NS_ASSERT(position != 0);
			Vector pos = position->GetPosition();
			std::cout << "Position: x=" << pos.x << ", y=" << pos.y << ", z="
					<< pos.z << std::endl;

			std::cout << std::endl;
		}
	}

	// evaluation-----------------------------------
	monitor->CheckForLostPackets();

	Time lastPacketRxTime;
	double avgThroughput = 0.0;
	int nValidFlow = 0;
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
						- i->second.timeFirstTxPacket.GetSeconds()) / 1024
				/ 1024;

		std::cout << "Flow " << i->first /*<< " (" << t.sourceAddress << " -> "
		 << t.destinationAddress*/<< ")\n";
		std::cout << " Tx Bytes: " << i->second.txBytes << "\n";
		std::cout << " Rx Bytes: " << i->second.rxBytes << "\n";
		std::cout << " Tx packets: " << i->second.txPackets << "\n";
		std::cout << " Rx packets: " << i->second.rxPackets << "\n";
		std::cout << " Throughput: " << th << " Mbps\n";

		if(i->second.rxPackets >= VALID_FLOW_COUNT)
		{
			avgThroughput += th;
			++nValidFlow;
		}

	}
	avgThroughput = avgThroughput / nValidFlow;
	std::cout << std::endl << "Average Throughput: " << avgThroughput << " Mbps"
			<< std::endl << std::endl;

	// channel utility
	Time avgIdleTime;
	Time totalRtsDuration;
	Time totalCtsDuration;
	for (uint32_t i = 0; i < staNodeContainer.GetN(); ++i)
	{
		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				staDataDevContainer.Get(i));
		Ptr<YansWifiPhy> dataPhy = DynamicCast<YansWifiPhy>(
				dataNetDevice->GetPhy());
		Time dataIdleTime = dataPhy->m_lastCumulateIdleTime;
		avgIdleTime += dataIdleTime;

		Ptr<StaDataWifiMac> staDataMac = DynamicCast<StaDataWifiMac>(
				dataNetDevice->GetMac());
		Ptr<DataDcaTxop> staDataDca = staDataMac->m_dca;
		totalRtsDuration += staDataDca->Low()->m_myRtsDuration;
		totalCtsDuration += staDataDca->Low()->m_myCtsDuration;
	}
	for (uint32_t i = 0; i < apNodeContainer.GetN(); ++i)
	{
		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				apDataDevContainer.Get(i));
		Ptr<YansWifiPhy> dataPhy = DynamicCast<YansWifiPhy>(
				dataNetDevice->GetPhy());
		Time dataIdleTime = dataPhy->m_lastCumulateIdleTime;
		avgIdleTime += dataIdleTime;

		Ptr<ApWifiMacData> apDataMac = DynamicCast<ApWifiMacData>(
				dataNetDevice->GetMac());
		Ptr<DataDcaTxop> apDataDca = apDataMac->m_dca;
		totalRtsDuration += apDataDca->Low()->m_myRtsDuration;
		totalCtsDuration += apDataDca->Low()->m_myCtsDuration;

	}

	avgIdleTime = Time(avgIdleTime.GetDouble() / (nSta + nAp));

	double percentage = (lastPacketRxTime.GetDouble() - avgIdleTime.GetDouble()
			- totalRtsDuration.GetDouble() - totalCtsDuration.GetDouble())
			/ lastPacketRxTime.GetDouble();
	std::cout << "Channel utility=" << percentage * 100 << "%" << std::endl;

	Simulator::Destroy();
}

void baseline(uint32_t nStaNum, uint32_t nUdpMaxPktCount, uint32_t nUdpPktSize,
		bool rts, bool hidden)
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

	uint32_t nMaxPktSize = nUdpPktSize;
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
		NS_LOG_ERROR("install svr on STA_" << i);
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

		NS_LOG_ERROR("install client on STA_" << i << ", bind to svr on STA_"
				<< i - 1);
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

	if (bLog)
	{
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
	}

// evaluation
	monitor->CheckForLostPackets();

	Time lastPacketRxTime;
	double avgThroughput = 0.0;
	int nValidFlow =  0;
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
						- i->second.timeFirstTxPacket.GetSeconds()) / 1024
				/ 1024;

		std::cout << "Flow " << i->first /*<< " (" << t.sourceAddress << " -> "
		 << t.destinationAddress*/<< ")\n";
		std::cout << " Tx Bytes: " << i->second.txBytes << "\n";
		std::cout << " Rx Bytes: " << i->second.rxBytes << "\n";
		std::cout << " Tx packets: " << i->second.txPackets << "\n";
		std::cout << " Rx packets: " << i->second.rxPackets << "\n";
		std::cout << " Throughput: " << th << " Mbps\n";

		if(i->second.rxPackets >= VALID_FLOW_COUNT)
		{
			avgThroughput += th;
			++nValidFlow;
		}

	}
	avgThroughput = avgThroughput / nValidFlow;
	std::cout << std::endl << "Average Throughput: " << avgThroughput << " Mbps"
			<< std::endl << std::endl;

	// channel utility
	Time totalRtsDuration;
	Time totalCtsDuration;
	Time avgIdleTime;
	for (uint32_t i = 0; i < staNodeContainer.GetN(); ++i)
	{
		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				staDataDevContainer.Get(i));

		Ptr<YansWifiPhy> dataPhy = DynamicCast<YansWifiPhy>(
				dataNetDevice->GetPhy());
		Time dataIdleTime = dataPhy->m_lastCumulateIdleTime;
		avgIdleTime += dataIdleTime;

		Ptr<StaWifiMac> staDataMac = DynamicCast<StaWifiMac>(
				dataNetDevice->GetMac());
		Ptr<DcaTxop> staDataDca = staDataMac->m_dca;
		totalRtsDuration += staDataDca->Low()->m_myRtsDuration;
		totalCtsDuration += staDataDca->Low()->m_myCtsDuration;
	}
	for (uint32_t i = 0; i < apNodeContainer.GetN(); ++i)
	{
		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				apDataDevContainer.Get(i));

		Ptr<YansWifiPhy> dataPhy = DynamicCast<YansWifiPhy>(
				dataNetDevice->GetPhy());
		Time dataIdleTime = dataPhy->m_lastCumulateIdleTime;
		avgIdleTime += dataIdleTime;

		Ptr<ApWifiMac> apDataMac = DynamicCast<ApWifiMac>(
				dataNetDevice->GetMac());
		Ptr<DcaTxop> apDataDca = apDataMac->m_dca;
		totalRtsDuration += apDataDca->Low()->m_myRtsDuration;
		totalCtsDuration += apDataDca->Low()->m_myCtsDuration;
	}

	avgIdleTime = Time(avgIdleTime.GetDouble() / (nSta + nAp));

	double percentage = (lastPacketRxTime.GetDouble() - avgIdleTime.GetDouble()
			- totalRtsDuration.GetDouble() - totalCtsDuration.GetDouble() )
			/ lastPacketRxTime.GetDouble();
	std::cout << "Channel utility=" << percentage * 100 << "%" << std::endl;

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

