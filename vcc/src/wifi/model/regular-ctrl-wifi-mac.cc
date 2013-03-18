/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
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
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "regular-ctrl-wifi-mac.h"

#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/pointer.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"

#include "mac-rx-middle.h"
#include "mac-tx-middle.h"
#include "mac-low-ctrl.h"
#include "dcf.h"
#include "dcf-manager.h"
#include "wifi-phy.h"

#include "msdu-aggregator.h"

NS_LOG_COMPONENT_DEFINE("RegularCtrlWifiMac");

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(RegularCtrlWifiMac);

RegularCtrlWifiMac::RegularCtrlWifiMac()
{
	NS_LOG_FUNCTION (this);
	m_rxMiddle = new MacRxMiddle();
	m_rxMiddle->SetForwardCallback(
			MakeCallback(&RegularCtrlWifiMac::Receive, this));

	m_txMiddle = new MacTxMiddle();

	m_low = CreateObject<MacLowCtrl>();
	m_low->SetRxCallback(MakeCallback(&MacRxMiddle::Receive, m_rxMiddle));

	m_dcfManager = new DcfManager();
	m_dcfManager->SetupLowListener(m_low);

	m_dca = CreateObject<CtrlDcaTxop>();
	m_dca->SetLow(m_low);
	m_dca->SetManager(m_dcfManager);
	m_dca->SetTxOkCallback(MakeCallback(&RegularCtrlWifiMac::TxOk, this));
	m_dca->SetTxFailedCallback(
			MakeCallback(&RegularCtrlWifiMac::TxFailed, this));

	// Construct the EDCAFs. The ordering is important - highest
	// priority (see Table 9-1 in IEEE 802.11-2007) must be created
	// first.
	SetupEdcaQueue(AC_VO);
	SetupEdcaQueue(AC_VI);
	SetupEdcaQueue(AC_BE);
	SetupEdcaQueue(AC_BK);
}

RegularCtrlWifiMac::~RegularCtrlWifiMac()
{
	NS_LOG_FUNCTION (this);
}

void RegularCtrlWifiMac::DoStart()
{
	NS_LOG_FUNCTION (this);

	m_dca->Start();

	for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i)
	{
		i->second->Start();
	}
}

void RegularCtrlWifiMac::DoDispose()
{
	NS_LOG_FUNCTION (this);
	delete m_rxMiddle;
	m_rxMiddle = NULL;

	delete m_txMiddle;
	m_txMiddle = NULL;

	delete m_dcfManager;
	m_dcfManager = NULL;

	m_low->Dispose();
	m_low = NULL;

	m_phy = NULL;
	m_stationManager = NULL;

	m_dca->Dispose();
	m_dca = NULL;

	for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i)
	{
		i->second = NULL;
	}
}

void RegularCtrlWifiMac::SetWifiRemoteStationManager(
		Ptr<WifiRemoteStationManager> stationManager)
{
	NS_LOG_FUNCTION (this << stationManager);
	m_stationManager = stationManager;
	m_low->SetWifiRemoteStationManager(stationManager);

	m_dca->SetWifiRemoteStationManager(stationManager);

	for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i)
	{
		i->second->SetWifiRemoteStationManager(stationManager);
	}
}

Ptr<WifiRemoteStationManager> RegularCtrlWifiMac::GetWifiRemoteStationManager() const
{
	return m_stationManager;
}

void RegularCtrlWifiMac::SetupEdcaQueue(enum AcIndex ac)
{
	NS_LOG_FUNCTION (this << ac);

	// Our caller shouldn't be attempting to setup a queue that is
	// already configured.
	NS_ASSERT(m_edca.find (ac) == m_edca.end ());

	Ptr<EdcaTxopN> edca = CreateObject<EdcaTxopN>();
	edca->SetLow(m_low);
	edca->SetManager(m_dcfManager);
	edca->SetTxMiddle(m_txMiddle);
	edca->SetTxOkCallback(MakeCallback(&RegularCtrlWifiMac::TxOk, this));
	edca->SetTxFailedCallback(
			MakeCallback(&RegularCtrlWifiMac::TxFailed, this));
	edca->SetAccessCategory(ac);
	edca->CompleteConfig();
	m_edca.insert(std::make_pair(ac, edca));
}

void RegularCtrlWifiMac::SetTypeOfStation(TypeOfStation type)
{
	NS_LOG_FUNCTION (this << type);
	for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i)
	{
		i->second->SetTypeOfStation(type);
	}
}

Ptr<EdcaTxopN> RegularCtrlWifiMac::GetVOQueue() const
{
	return m_edca.find(AC_VO)->second;
}

Ptr<EdcaTxopN> RegularCtrlWifiMac::GetVIQueue() const
{
	return m_edca.find(AC_VI)->second;
}

Ptr<EdcaTxopN> RegularCtrlWifiMac::GetBEQueue() const
{
	return m_edca.find(AC_BE)->second;
}

Ptr<EdcaTxopN> RegularCtrlWifiMac::GetBKQueue() const
{
	return m_edca.find(AC_BK)->second;
}

void RegularCtrlWifiMac::SetWifiPhy(Ptr<WifiPhy> phy)
{
	NS_LOG_FUNCTION (this << phy);
	m_phy = phy;
	m_dcfManager->SetupPhyListener(phy);
	m_low->SetPhy(phy);
}

Ptr<WifiPhy> RegularCtrlWifiMac::GetWifiPhy() const
{
	return m_phy;
}

void RegularCtrlWifiMac::SetForwardUpCallback(ForwardUpCallback upCallback)
{
	NS_LOG_FUNCTION (this);
	m_forwardUp = upCallback;
}

void RegularCtrlWifiMac::SetLinkUpCallback(Callback<void> linkUp)
{
	NS_LOG_FUNCTION (this);
	m_linkUp = linkUp;
}

void RegularCtrlWifiMac::SetLinkDownCallback(Callback<void> linkDown)
{
	NS_LOG_FUNCTION (this);
	m_linkDown = linkDown;
}

void RegularCtrlWifiMac::SetQosSupported(bool enable)
{
	NS_LOG_FUNCTION (this);
	m_qosSupported = enable;
}

bool RegularCtrlWifiMac::GetQosSupported() const
{
	return m_qosSupported;
}

void RegularCtrlWifiMac::SetSlot(Time slotTime)
{
	NS_LOG_FUNCTION (this << slotTime);
	m_dcfManager->SetSlot(slotTime);
	m_low->SetSlotTime(slotTime);
}

Time RegularCtrlWifiMac::GetSlot(void) const
{
	return m_low->GetSlotTime();
}

void RegularCtrlWifiMac::SetSifs(Time sifs)
{
	NS_LOG_FUNCTION (this << sifs);
	m_dcfManager->SetSifs(sifs);
	m_low->SetSifs(sifs);
}

Time RegularCtrlWifiMac::GetSifs(void) const
{
	return m_low->GetSifs();
}

void RegularCtrlWifiMac::SetEifsNoDifs(Time eifsNoDifs)
{
	NS_LOG_FUNCTION (this << eifsNoDifs);
	m_dcfManager->SetEifsNoDifs(eifsNoDifs);
}

Time RegularCtrlWifiMac::GetEifsNoDifs(void) const
{
	return m_dcfManager->GetEifsNoDifs();
}

void RegularCtrlWifiMac::SetPifs(Time pifs)
{
	NS_LOG_FUNCTION (this << pifs);
	m_low->SetPifs(pifs);
}

Time RegularCtrlWifiMac::GetPifs(void) const
{
	return m_low->GetPifs();
}

void RegularCtrlWifiMac::SetAckTimeout(Time ackTimeout)
{
	NS_LOG_FUNCTION (this << ackTimeout);
	m_low->SetAckTimeout(ackTimeout);
}

Time RegularCtrlWifiMac::GetAckTimeout(void) const
{
	return m_low->GetAckTimeout();
}

void RegularCtrlWifiMac::SetCtsTimeout(Time ctsTimeout)
{
	NS_LOG_FUNCTION (this << ctsTimeout);
	m_low->SetCtsTimeout(ctsTimeout);
}

Time RegularCtrlWifiMac::GetCtsTimeout(void) const
{
	return m_low->GetCtsTimeout();
}

void RegularCtrlWifiMac::SetBasicBlockAckTimeout(Time blockAckTimeout)
{
	NS_LOG_FUNCTION (this << blockAckTimeout);
	m_low->SetBasicBlockAckTimeout(blockAckTimeout);
}

Time RegularCtrlWifiMac::GetBasicBlockAckTimeout(void) const
{
	return m_low->GetBasicBlockAckTimeout();
}

void RegularCtrlWifiMac::SetCompressedBlockAckTimeout(Time blockAckTimeout)
{
	NS_LOG_FUNCTION (this << blockAckTimeout);
	m_low->SetCompressedBlockAckTimeout(blockAckTimeout);
}

Time RegularCtrlWifiMac::GetCompressedBlockAckTimeout(void) const
{
	return m_low->GetCompressedBlockAckTimeout();
}

void RegularCtrlWifiMac::SetAddress(Mac48Address address)
{
	NS_LOG_FUNCTION (this << address);
	m_low->SetAddress(address);
}

Mac48Address RegularCtrlWifiMac::GetAddress(void) const
{
	return m_low->GetAddress();
}

void RegularCtrlWifiMac::SetSsid(Ssid ssid)
{
	NS_LOG_FUNCTION (this << ssid);
	m_ssid = ssid;
}

Ssid RegularCtrlWifiMac::GetSsid(void) const
{
	return m_ssid;
}

void RegularCtrlWifiMac::SetBssid(Mac48Address bssid)
{
	NS_LOG_FUNCTION (this << bssid);
	m_low->SetBssid(bssid);
}

Mac48Address RegularCtrlWifiMac::GetBssid(void) const
{
	return m_low->GetBssid();
}

void RegularCtrlWifiMac::SetPromisc(void)
{
	m_low->SetPromisc();
}

void RegularCtrlWifiMac::Enqueue(Ptr<const Packet> packet, Mac48Address to,
		Mac48Address from)
{
	// We expect RegularCtrlWifiMac subclasses which do support forwarding (e.g.,
	// AP) to override this method. Therefore, we throw a fatal error if
	// someone tries to invoke this method on a class which has not done
	// this.
	NS_FATAL_ERROR(
			"This MAC entity (" << this << ", " << GetAddress () << ") does not support Enqueue() with from address");
}

bool RegularCtrlWifiMac::SupportsSendFrom(void) const
{
	return false;
}

void RegularCtrlWifiMac::ForwardUp(Ptr<Packet> packet, Mac48Address from,
		Mac48Address to)
{
	NS_LOG_FUNCTION (this << packet << from);
	m_forwardUp(packet, from, to);
}

void RegularCtrlWifiMac::Receive(Ptr<Packet> packet, const WifiMacHeader *hdr)
{
	NS_LOG_FUNCTION (this << packet << hdr);

	Mac48Address to = hdr->GetAddr1();
	Mac48Address from = hdr->GetAddr2();

	// We don't know how to deal with any frame that is not addressed to
	// us (and odds are there is nothing sensible we could do anyway),
	// so we ignore such frames.
	//
	// The derived class may also do some such filtering, but it doesn't
	// hurt to have it here too as a backstop.
	if (to != GetAddress())
	{
		return;
	}

	if (hdr->IsMgt() && hdr->IsAction())
	{
		// There is currently only any reason for Management Action
		// frames to be flying about if we are a QoS STA.
		NS_ASSERT(m_qosSupported);

		WifiActionHeader actionHdr;
		packet->RemoveHeader(actionHdr);

		switch (actionHdr.GetCategory())
		{
		case WifiActionHeader::BLOCK_ACK:

			switch (actionHdr.GetAction().blockAck)
			{
			case WifiActionHeader::BLOCK_ACK_ADDBA_REQUEST:
			{
				MgtAddBaRequestHeader reqHdr;
				packet->RemoveHeader(reqHdr);

				// We've received an ADDBA Request. Our policy here is
				// to automatically accept it, so we get the ADDBA
				// Response on it's way immediately.
				SendAddBaResponse(&reqHdr, from);
				// This frame is now completely dealt with, so we're done.
				return;
			}

			case WifiActionHeader::BLOCK_ACK_ADDBA_RESPONSE:
			{
				MgtAddBaResponseHeader respHdr;
				packet->RemoveHeader(respHdr);

				// We've received an ADDBA Response. We assume that it
				// indicates success after an ADDBA Request we have
				// sent (we could, in principle, check this, but it
				// seems a waste given the level of the current model)
				// and act by locally establishing the agreement on
				// the appropriate queue.
				AcIndex ac = QosUtilsMapTidToAc(respHdr.GetTid());
				m_edca[ac]->GotAddBaResponse(&respHdr, from);
				// This frame is now completely dealt with, so we're done.
				return;
			}

			case WifiActionHeader::BLOCK_ACK_DELBA:
			{
				MgtDelBaHeader delBaHdr;
				packet->RemoveHeader(delBaHdr);

				if (delBaHdr.IsByOriginator())
				{
					// This DELBA frame was sent by the originator, so
					// this means that an ingoing established
					// agreement exists in MacLow and we need to
					// destroy it.
					m_low->DestroyBlockAckAgreement(from, delBaHdr.GetTid());
				}
				else
				{
					// We must have been the originator. We need to
					// tell the correct queue that the agreement has
					// been torn down
					AcIndex ac = QosUtilsMapTidToAc(delBaHdr.GetTid());
					m_edca[ac]->GotDelBaFrame(&delBaHdr, from);
				}
				// This frame is now completely dealt with, so we're done.
				return;
			}

			default:
				NS_FATAL_ERROR(
						"Unsupported Action field in Block Ack Action frame");
			}

		default:
			NS_FATAL_ERROR("Unsupported Action frame received");
		}
	}
	NS_FATAL_ERROR("Don't know how to handle frame (type=" << hdr->GetType ());
}

void RegularCtrlWifiMac::DeaggregateAmsduAndForward(
		Ptr<Packet> aggregatedPacket, const WifiMacHeader *hdr)
{
	MsduAggregator::DeaggregatedMsdus packets = MsduAggregator::Deaggregate(
			aggregatedPacket);

	for (MsduAggregator::DeaggregatedMsdusCI i = packets.begin();
			i != packets.end(); ++i)
	{
		ForwardUp((*i).first, (*i).second.GetSourceAddr(),
				(*i).second.GetDestinationAddr());
	}
}

void RegularCtrlWifiMac::SendAddBaResponse(const MgtAddBaRequestHeader *reqHdr,
		Mac48Address originator)
{
	NS_LOG_FUNCTION (this);
	WifiMacHeader hdr;
	hdr.SetAction();
	hdr.SetAddr1(originator);
	hdr.SetAddr2(GetAddress());
	hdr.SetAddr3(GetAddress());
	hdr.SetDsNotFrom();
	hdr.SetDsNotTo();

	MgtAddBaResponseHeader respHdr;
	StatusCode code;
	code.SetSuccess();
	respHdr.SetStatusCode(code);
	//Here a control about queues type?
	respHdr.SetAmsduSupport(reqHdr->IsAmsduSupported());

	if (reqHdr->IsImmediateBlockAck())
	{
		respHdr.SetImmediateBlockAck();
	}
	else
	{
		respHdr.SetDelayedBlockAck();
	}
	respHdr.SetTid(reqHdr->GetTid());
	// For now there's not no control about limit of reception. We
	// assume that receiver has no limit on reception. However we assume
	// that a receiver sets a bufferSize in order to satisfy next
	// equation: (bufferSize + 1) % 16 = 0 So if a recipient is able to
	// buffer a packet, it should be also able to buffer all possible
	// packet's fragments. See section 7.3.1.14 in IEEE802.11e for more
	// details.
	respHdr.SetBufferSize(1023);
	respHdr.SetTimeout(reqHdr->GetTimeout());

	WifiActionHeader actionHdr;
	WifiActionHeader::ActionValue action;
	action.blockAck = WifiActionHeader::BLOCK_ACK_ADDBA_RESPONSE;
	actionHdr.SetAction(WifiActionHeader::BLOCK_ACK, action);

	Ptr<Packet> packet = Create<Packet>();
	packet->AddHeader(respHdr);
	packet->AddHeader(actionHdr);

	// We need to notify our MacLow object as it will have to buffer all
	// correctly received packets for this Block Ack session
	m_low->CreateBlockAckAgreement(&respHdr, originator,
			reqHdr->GetStartingSequence());

	// It is unclear which queue this frame should go into. For now we
	// bung it into the queue corresponding to the TID for which we are
	// establishing an agreement, and push it to the head.
	m_edca[QosUtilsMapTidToAc(reqHdr->GetTid())]->PushFront(packet, hdr);
}

TypeId RegularCtrlWifiMac::GetTypeId(void)
{
	static TypeId tid =
			TypeId("ns3::RegularCtrlWifiMac").SetParent<WifiMac>().AddAttribute(
					"QosSupported",
					"This Boolean attribute is set to enable 802.11e/WMM-style QoS support at this STA",
					BooleanValue(false),
					MakeBooleanAccessor(&RegularCtrlWifiMac::SetQosSupported,
							&RegularCtrlWifiMac::GetQosSupported),
					MakeBooleanChecker()).AddAttribute("VO_EdcaTxopN",
					"Queue that manages packets belonging to AC_VO access class",
					PointerValue(),
					MakePointerAccessor(&RegularCtrlWifiMac::GetVOQueue),
					MakePointerChecker<EdcaTxopN>()).AddAttribute(
					"VI_EdcaTxopN",
					"Queue that manages packets belonging to AC_VI access class",
					PointerValue(),
					MakePointerAccessor(&RegularCtrlWifiMac::GetVIQueue),
					MakePointerChecker<EdcaTxopN>()).AddAttribute(
					"BE_EdcaTxopN",
					"Queue that manages packets belonging to AC_BE access class",
					PointerValue(),
					MakePointerAccessor(&RegularCtrlWifiMac::GetBEQueue),
					MakePointerChecker<EdcaTxopN>()).AddAttribute(
					"BK_EdcaTxopN",
					"Queue that manages packets belonging to AC_BK access class",
					PointerValue(),
					MakePointerAccessor(&RegularCtrlWifiMac::GetBKQueue),
					MakePointerChecker<EdcaTxopN>()).AddTraceSource(
					"TxOkHeader",
					"The header of successfully transmitted packet",
					MakeTraceSourceAccessor(
							&RegularCtrlWifiMac::m_txOkCallback)).AddTraceSource(
					"TxErrHeader",
					"The header of unsuccessfully transmitted packet",
					MakeTraceSourceAccessor(
							&RegularCtrlWifiMac::m_txErrCallback));

	return tid;
}

void RegularCtrlWifiMac::FinishConfigureStandard(enum WifiPhyStandard standard)
{
	uint32_t cwmin;
	uint32_t cwmax;

	switch (standard)
	{
	case WIFI_PHY_STANDARD_80211p_CCH:
	case WIFI_PHY_STANDARD_80211p_SCH:
		cwmin = 15;
		cwmax = 511;
		break;

	case WIFI_PHY_STANDARD_holland:
	case WIFI_PHY_STANDARD_80211a:
	case WIFI_PHY_STANDARD_80211g:
	case WIFI_PHY_STANDARD_80211_10MHZ:
	case WIFI_PHY_STANDARD_80211_5MHZ:
		cwmin = 15;
		cwmax = 1023;
		break;

	case WIFI_PHY_STANDARD_80211b:
		cwmin = 31;
		cwmax = 1023;
		break;

	default:
		NS_FATAL_ERROR(
				"Unsupported WifiPhyStandard in RegularCtrlWifiMac::FinishConfigureStandard ()");
	}

	// The special value of AC_BE_NQOS which exists in the Access
	// Category enumeration allows us to configure plain old DCF.
	ConfigureDcf(m_dca, cwmin, cwmax, AC_BE_NQOS);

	// Now we configure the EDCA functions
	for (EdcaQueues::iterator i = m_edca.begin(); i != m_edca.end(); ++i)
	{
		// Special configuration for 802.11p CCH
		if (standard == WIFI_PHY_STANDARD_80211p_CCH)
		{
			ConfigureCCHDcf(i->second, cwmin, cwmax, i->first);
		}
		else
		{
			ConfigureDcf(i->second, cwmin, cwmax, i->first);
		}
	}
}

void RegularCtrlWifiMac::TxOk(const WifiMacHeader &hdr)
{
	NS_LOG_FUNCTION (this << hdr);
	m_txOkCallback(hdr);
}

void RegularCtrlWifiMac::TxFailed(const WifiMacHeader &hdr)
{
	NS_LOG_FUNCTION (this << hdr);
	m_txErrCallback(hdr);
}

} // namespace ns3
