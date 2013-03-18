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

// Default Network Topology
//
//  Optional    Basic
//           |
// cli--srv  | cli--svr   AP
//  *    *   |  *    *    *
//  |    |   |  |    |    |
// n3   n2   | n1   n0	ap_0
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("eva");

int main(int argc, char *argv[])
{
	bool verbose = true;
	uint32_t nSta = 4;
	uint32_t nPort = 1987;
	bool g_bGlobalFirstRts = true;

	if (verbose)
	{
//		LogComponentEnable("LinUdpServer", LOG_LEVEL_INFO);
//		LogComponentEnable("LinUdpClient", LOG_LEVEL_INFO);

		LogComponentEnableAll(LOG_NONE);

	}
	LogComponentEnable("eva", LOG_LEVEL_INFO);

	// create nodes
	NodeContainer wifiStaNodes;
	wifiStaNodes.Create(nSta);

	NodeContainer wifiApNode;
	wifiApNode.Create(1); //only one AP

	// setup data rate control algorithm
	WifiHelper wifiHelper = WifiHelper::Default();
	wifiHelper.SetRemoteStationManager("ns3::AarfWifiManager",
			"RtsCtsThreshold", UintegerValue(500)); // enable RTS & CTS

	// setup PHY & Channel
	YansWifiChannelHelper chnHelper = YansWifiChannelHelper::Default();
	YansWifiPhyHelper phyHelper = YansWifiPhyHelper::Default();
	phyHelper.SetChannel(chnHelper.Create());

	// ------------STA------------------
	// setup STA MAC
	NqosWifiMacHelper macHelper = NqosWifiMacHelper::Default();


	// install netDevices: data first, ctrl later
	Ssid dataSsid = Ssid("dual_data_ssid");
	macHelper.SetType("ns3::StaDataWifiMac", "Ssid", SsidValue(dataSsid),
			"ActiveProbing", BooleanValue(false));
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	NetDeviceContainer staDataDevicesContainer = wifiHelper.Install(phyHelper,
			macHelper, wifiStaNodes);

	Ssid ctrlSsid = Ssid("dual_ctrl_ssid");
	macHelper.SetType("ns3::StaCtrlWifiMac", "Ssid", SsidValue(ctrlSsid),
			"ActiveProbing", BooleanValue(false));
	phyHelper.Set("ChannelNumber", UintegerValue(6));
	NetDeviceContainer staCtrlDevicesContainer = wifiHelper.Install(phyHelper,
			macHelper, wifiStaNodes);

	// ------------AP------------------
	// setup AP MAC

	// install netDevices: data first, ctrl later
	macHelper.SetType("ns3::ApWifiMacData", "Ssid", SsidValue(dataSsid));
	phyHelper.Set("ChannelNumber", UintegerValue(1));
	NetDeviceContainer apDataDevicesContainer = wifiHelper.Install(phyHelper,
			macHelper, wifiApNode);

	macHelper.SetType("ns3::ApWifiMacCtrl", "Ssid", SsidValue(ctrlSsid));
	phyHelper.Set("ChannelNumber", UintegerValue(6));
	NetDeviceContainer apCtrlDevicesContainer = wifiHelper.Install(phyHelper,
			macHelper, wifiApNode);

	// -----------setup callbacks--------------------
	// NOTE: this MUST be done after all nodes have finished the device installation
	NodeContainer allNodes = wifiStaNodes;
	allNodes.Add(wifiApNode);

	// setup callbacks for STA
	for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
	{
		Ptr<WifiNetDevice> dataNetDevice = DynamicCast<WifiNetDevice>(
				staDataDevicesContainer.Get(i));
		Ptr<StaDataWifiMac> staDataMac = DynamicCast<StaDataWifiMac>(
				dataNetDevice->GetMac());
		Ptr<DataDcaTxop> staDataDca = staDataMac->m_dca;

		Ptr<WifiNetDevice> staCtrlNetDevice = DynamicCast<WifiNetDevice>(
				staCtrlDevicesContainer.Get(i));
		Ptr<StaCtrlWifiMac> staCtrlMac = DynamicCast<StaCtrlWifiMac>(
				staCtrlNetDevice->GetMac());
		Ptr<CtrlDcaTxop> staCtrlDca = staCtrlMac->m_dca;

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

	// setup callbacks for AP
	for (uint32_t i = 0; i < wifiApNode.GetN(); ++i)
	{
		Ptr<WifiNetDevice> apDataNetDevice = DynamicCast<WifiNetDevice>(
				apDataDevicesContainer.Get(i));
		Ptr<ApWifiMacData> apDataMac = DynamicCast<ApWifiMacData>(
				apDataNetDevice->GetMac());
		Ptr<DataDcaTxop> apDataDca = apDataMac->m_dca;

		Ptr<WifiNetDevice> apCtrlNetDevice = DynamicCast<WifiNetDevice>(
				apCtrlDevicesContainer.Get(i));
		Ptr<ApWifiMacCtrl> apCtrlMac = DynamicCast<ApWifiMacCtrl>(
				apCtrlNetDevice->GetMac());
		Ptr<CtrlDcaTxop> apCtrlDca = apCtrlMac->m_dca;

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

	// setup mobility model - constant position
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

	// assign ip address, data first, ctrl later
	Ipv4AddressHelper address;
	address.SetBase("192.168.0.0", "255.255.255.0");
	address.Assign(apDataDevicesContainer);
	Ipv4InterfaceContainer wifiIpInterfaceData = address.Assign(
			staDataDevicesContainer);

	address.SetBase("192.168.7.0", "255.255.255.0");
	address.Assign(apCtrlDevicesContainer);
	Ipv4InterfaceContainer wifiIpInterfaceCtrl = address.Assign(
			staCtrlDevicesContainer);

	// setup UDP server
	LinUdpServerHelper srvHelper(nPort);
	ApplicationContainer srvApp;
	for (uint32_t i = 0; i < wifiStaNodes.GetN(); i = i + 2)
	{
		srvApp.Add(
				srvHelper.Install(wifiStaNodes.Get(i),
						staDataDevicesContainer.Get(i)));
		std::cout << "install svr on STA_" << i << std::endl;
	}
	srvApp.Start(Seconds(1.0));
	srvApp.Stop(Seconds(60.0));

	// setup UDP client
	uint32_t nMaxPktSize = 1024; // note: if we change the pkt size, change the duration check in DataDcaTxop::NotifyRxStart()
	Time interPktInterval = MicroSeconds(1500);
	uint32_t nMaxPktCount = 5000;

	ApplicationContainer clientApp;
	for (uint32_t i = 1; i < wifiStaNodes.GetN(); i = i + 2)
	{
		LinUdpClientHelper clientHelper(wifiIpInterfaceData.GetAddress(i - 1),
				nPort);
		clientHelper.SetAttribute("MaxPackets", UintegerValue(nMaxPktCount));
		clientHelper.SetAttribute("Interval", TimeValue(interPktInterval));
		clientHelper.SetAttribute("PacketSize", UintegerValue(nMaxPktSize));
		clientApp.Add(
				clientHelper.Install(wifiStaNodes.Get(i),
						staDataDevicesContainer.Get(i)));

		std::cout << "install client on STA_" << i << ", bound to svr on STA_"
				<< i - 1 << std::endl;
	}

	clientApp.Start(Seconds(5.0));
	clientApp.Stop(Seconds(50.0));

	// prepare the flow monitor
	FlowMonitorHelper flowmon;
	Ptr<FlowMonitor> monitor = flowmon.InstallAll();

	// print out topology information-----------------------
	// STA information
	for (uint32_t i = 0; i < wifiStaNodes.GetN(); ++i)
	{
		Ptr<Node> staNode = wifiStaNodes.Get(i);

		std::cout << "STA_" << i << ":" << std::endl;

		// mac addr
		std::cout << "data NetDevice addr: "
				<< DynamicCast<WifiNetDevice>(staNode->GetDevice(0))->GetMac()->GetAddress()
				<< std::endl;
		std::cout << "ctrl NetDevice addr: "
				<< DynamicCast<WifiNetDevice>(staNode->GetDevice(1))->GetMac()->GetAddress()
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
		std::cout << "data NetDevice addr: "
				<< DynamicCast<WifiNetDevice>(apNode->GetDevice(0))->GetMac()->GetAddress()
				<< std::endl;
		std::cout << "ctrl NetDevice addr: "
				<< DynamicCast<WifiNetDevice>(apNode->GetDevice(1))->GetMac()->GetAddress()
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

	std::cout << "Simulator ends:dual" << std::endl;

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
