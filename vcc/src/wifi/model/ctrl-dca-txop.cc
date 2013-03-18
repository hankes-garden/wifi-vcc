/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005 INRIA
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

#include "ns3/assert.h"
#include "ns3/packet.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/node.h"
#include "ns3/uinteger.h"
#include "ns3/pointer.h"

#include "ctrl-dca-txop.h"
#include "dcf-manager.h"
#include "mac-low.h"
#include "wifi-mac-queue.h"
#include "mac-tx-middle.h"
#include "wifi-mac-trailer.h"
#include "wifi-mac.h"
#include "random-stream.h"
#include "wifi-net-device.h"

NS_LOG_COMPONENT_DEFINE("CtrlDcaTxop");

#undef NS_LOG_APPEND_CONTEXT
#define NS_LOG_APPEND_CONTEXT if (m_low != 0) { std::clog << "[mac=" << m_low->GetAddress () << "] "; }

namespace ns3
{

class CtrlDcaTxop::Dcf: public DcfState
{
public:
	Dcf(CtrlDcaTxop * txop) :
			m_txop(txop)
	{
	}
private:
	virtual void DoNotifyAccessGranted(void)
	{
		m_txop->NotifyAccessGranted();
	}
	virtual void DoNotifyInternalCollision(void)
	{
		m_txop->NotifyInternalCollision();
	}
	virtual void DoNotifyCollision(void)
	{
		m_txop->NotifyCollision();
	}
	virtual void DoNotifyChannelSwitching(void)
	{
		m_txop->NotifyChannelSwitching();
	}
	CtrlDcaTxop *m_txop;
};

class CtrlDcaTxop::TransmissionListener: public MacLowTransmissionListener
{
public:
	TransmissionListener(CtrlDcaTxop * txop) :
			MacLowTransmissionListener(), m_txop(txop)
	{
	}

	virtual ~TransmissionListener()
	{
	}

	virtual void GotCts(double snr, WifiMode txMode)
	{
		m_txop->GotCts(snr, txMode);
	}
	virtual void MissedCts(void)
	{
		m_txop->MissedCts();
	}
	virtual void GotAck(double snr, WifiMode txMode)
	{
		m_txop->GotAck(snr, txMode);
	}
	virtual void MissedAck(void)
	{
		m_txop->MissedAck();
	}
	virtual void StartNext(void)
	{
		m_txop->StartNext();
	}
	virtual void Cancel(void)
	{
		m_txop->Cancel();
	}
	virtual void EndTxNoAck(void)
	{
		m_txop->EndTxNoAck();
	}

private:
	CtrlDcaTxop *m_txop;
};

NS_OBJECT_ENSURE_REGISTERED(CtrlDcaTxop);

TypeId CtrlDcaTxop::GetTypeId(void)
{
	static TypeId tid = TypeId("ns3::CtrlDcaTxop").SetParent(
			ns3::Dcf::GetTypeId()).AddConstructor<CtrlDcaTxop>().AddAttribute(
			"Queue", "The WifiMacQueue object", PointerValue(),
			MakePointerAccessor(&CtrlDcaTxop::GetQueue),
			MakePointerChecker<WifiMacQueue>());
	return tid;
}

CtrlDcaTxop::CtrlDcaTxop() :
		m_manager(0), m_currentPacket(0)
{
	NS_LOG_FUNCTION (this);
	m_transmissionListener = new CtrlDcaTxop::TransmissionListener(this);
	m_dcf = new CtrlDcaTxop::Dcf(this);
	m_queue = CreateObject<WifiMacQueue>();
	m_rng = new RealRandomStream();
	m_txMiddle = new MacTxMiddle();

	m_pNodeContainer = 0;

	NS_LOG_INFO("This is ctrl DcaTxop");
}

CtrlDcaTxop::~CtrlDcaTxop()
{
	NS_LOG_FUNCTION (this);
}

void CtrlDcaTxop::DoDispose(void)
{
	NS_LOG_FUNCTION (this);
	m_queue = 0;
	m_low = 0;
	m_stationManager = 0;
	delete m_transmissionListener;
	delete m_dcf;
	delete m_rng;
	delete m_txMiddle;
	m_transmissionListener = 0;
	m_dcf = 0;
	m_rng = 0;
	m_txMiddle = 0;
}

void CtrlDcaTxop::SetManager(DcfManager *manager)
{
	NS_LOG_FUNCTION (this << manager);
	m_manager = manager;
	m_manager->Add(m_dcf);
}

void CtrlDcaTxop::SetLow(Ptr<MacLowCtrl> low)
{
	NS_LOG_FUNCTION (this << low);
	m_low = low;
	m_low->SetNotifyDataChannelCallback(
			MakeCallback(&CtrlDcaTxop::NotifyDataChannel, this));
}
void CtrlDcaTxop::SetWifiRemoteStationManager(
		Ptr<WifiRemoteStationManager> remoteManager)
{
	NS_LOG_FUNCTION (this << remoteManager);
	m_stationManager = remoteManager;
}
void CtrlDcaTxop::SetTxOkCallback(TxOk callback)
{
	m_txOkCallback = callback;
}
void CtrlDcaTxop::SetTxFailedCallback(TxFailed callback)
{
	m_txFailedCallback = callback;
}

Ptr<WifiMacQueue> CtrlDcaTxop::GetQueue() const
{
	NS_LOG_FUNCTION (this);
	return m_queue;
}

void CtrlDcaTxop::SetMinCw(uint32_t minCw)
{
	NS_LOG_FUNCTION (this << minCw);
	m_dcf->SetCwMin(minCw);
}
void CtrlDcaTxop::SetMaxCw(uint32_t maxCw)
{
	NS_LOG_FUNCTION (this << maxCw);
	m_dcf->SetCwMax(maxCw);
}
void CtrlDcaTxop::SetAifsn(uint32_t aifsn)
{
	NS_LOG_FUNCTION (this << aifsn);
	m_dcf->SetAifsn(aifsn);
}
uint32_t CtrlDcaTxop::GetMinCw(void) const
{
	return m_dcf->GetCwMin();
}
uint32_t CtrlDcaTxop::GetMaxCw(void) const
{
	return m_dcf->GetCwMax();
}
uint32_t CtrlDcaTxop::GetAifsn(void) const
{
	return m_dcf->GetAifsn();
}

void CtrlDcaTxop::Queue(Ptr<const Packet> packet, const WifiMacHeader &hdr)
{
	NS_LOG_FUNCTION (this << packet << &hdr);
	WifiMacTrailer fcs;
	uint32_t fullPacketSize = hdr.GetSerializedSize() + packet->GetSize()
			+ fcs.GetSerializedSize();
	m_stationManager->PrepareForQueue(hdr.GetAddr1(), &hdr, packet,
			fullPacketSize);
	m_queue->Enqueue(packet, hdr);
	StartAccessIfNeeded();
}

int64_t CtrlDcaTxop::AssignStreams(int64_t stream)
{
	NS_LOG_FUNCTION (this << stream);
	m_rng->AssignStreams(stream);
	return 1;
}

void CtrlDcaTxop::RestartAccessIfNeeded(void)
{
	NS_LOG_FUNCTION (this);
	if ((m_currentPacket != 0 || !m_queue->IsEmpty())
			&& !m_dcf->IsAccessRequested())
	{
		m_manager->RequestAccess(m_dcf);
	}
}

void CtrlDcaTxop::StartAccessIfNeeded(void)
{
	NS_LOG_FUNCTION (this);
	if (m_currentPacket == 0 && !m_queue->IsEmpty()
			&& !m_dcf->IsAccessRequested())
	{
		m_manager->RequestAccess(m_dcf);
	}
}

Ptr<MacLowCtrl> CtrlDcaTxop::Low(void)
{
	return m_low;
}

bool CtrlDcaTxop::NeedRts(Ptr<const Packet> packet, const WifiMacHeader *header)
{
	return m_stationManager->NeedRts(header->GetAddr1(), header, packet);
}

void CtrlDcaTxop::DoStart()
{
	m_dcf->ResetCw();
	m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
	ns3::Dcf::DoStart();
}
bool CtrlDcaTxop::NeedRtsRetransmission(void)
{
	return m_stationManager->NeedRtsRetransmission(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket);
}

bool CtrlDcaTxop::NeedDataRetransmission(void)
{
	return m_stationManager->NeedDataRetransmission(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket);
}
bool CtrlDcaTxop::NeedFragmentation(void)
{
	return m_stationManager->NeedFragmentation(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket);
}

void CtrlDcaTxop::NextFragment(void)
{
	m_fragmentNumber++;
}

uint32_t CtrlDcaTxop::GetFragmentSize(void)
{
	return m_stationManager->GetFragmentSize(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket, m_fragmentNumber);
}
bool CtrlDcaTxop::IsLastFragment(void)
{
	return m_stationManager->IsLastFragment(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket, m_fragmentNumber);
}

uint32_t CtrlDcaTxop::GetNextFragmentSize(void)
{
	return m_stationManager->GetFragmentSize(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket, m_fragmentNumber + 1);
}

uint32_t CtrlDcaTxop::GetFragmentOffset(void)
{
	return m_stationManager->GetFragmentOffset(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket, m_fragmentNumber);
}

Ptr<Packet> CtrlDcaTxop::GetFragmentPacket(WifiMacHeader *hdr)
{
	*hdr = m_currentHdr;
	hdr->SetFragmentNumber(m_fragmentNumber);
	uint32_t startOffset = GetFragmentOffset();
	Ptr<Packet> fragment;
	if (IsLastFragment())
	{
		hdr->SetNoMoreFragments();
	}
	else
	{
		hdr->SetMoreFragments();
	}
	fragment = m_currentPacket->CreateFragment(startOffset, GetFragmentSize());
	return fragment;
}

bool CtrlDcaTxop::NeedsAccess(void) const
{
	return !m_queue->IsEmpty() || m_currentPacket != 0;
}
void CtrlDcaTxop::NotifyAccessGranted(void)
{
	NS_LOG_FUNCTION (this);
	if (m_currentPacket == 0)
	{
		if (m_queue->IsEmpty())
		{
			NS_LOG_DEBUG ("queue empty");
			return;
		}
		m_currentPacket = m_queue->Dequeue(&m_currentHdr);
		NS_ASSERT(m_currentPacket != 0);

		if (!m_currentHdr.IsRts() && !m_currentHdr.IsCts()) // RTS/CTS do NOT need this
		{
			uint16_t sequence = m_txMiddle->GetNextSequenceNumberfor(
					&m_currentHdr);
			m_currentHdr.SetSequenceNumber(sequence);
			m_currentHdr.SetFragmentNumber(0);
			m_currentHdr.SetNoMoreFragments();
			m_currentHdr.SetNoRetry();
			m_fragmentNumber = 0;
			NS_LOG_DEBUG ("dequeued size=" << m_currentPacket->GetSize () <<
					", to=" << m_currentHdr.GetAddr1 () <<
					", seq=" << m_currentHdr.GetSequenceControl ());

		}
	}

	MacLowTransmissionParameters params;
	params.DisableOverrideDurationId();

	if (m_currentHdr.IsRts() || m_currentHdr.IsCts()) // special process for RTS/CTS
	{
		params.DisableRts();
		params.DisableAck();
		params.DisableNextData();
		Low()->StartTransmission(m_currentPacket, &m_currentHdr, params,
				m_transmissionListener);
	}
	else // normal process
	{
		if (m_currentHdr.GetAddr1().IsGroup())
		{
			params.DisableRts();
			params.DisableAck();
			params.DisableNextData();
			Low()->StartTransmission(m_currentPacket, &m_currentHdr, params,
					m_transmissionListener);
			NS_LOG_DEBUG ("tx broadcast");
		}
		else
		{
			params.EnableAck();

			if (NeedFragmentation())
			{
				WifiMacHeader hdr;
				Ptr<Packet> fragment = GetFragmentPacket(&hdr);
				if (NeedRts(fragment, &hdr))
				{
					params.EnableRts();
				}
				else
				{
					params.DisableRts();
				}
				if (IsLastFragment())
				{
					NS_LOG_DEBUG ("fragmenting last fragment size=" << fragment->GetSize ());
					params.DisableNextData();
				}
				else
				{
					NS_LOG_DEBUG ("fragmenting size=" << fragment->GetSize ());
					params.EnableNextData(GetNextFragmentSize());
				}
				Low()->StartTransmission(fragment, &hdr, params,
						m_transmissionListener);
			}
			else
			{
				if (NeedRts(m_currentPacket, &m_currentHdr))
				{
					params.EnableRts();
					NS_LOG_DEBUG ("tx unicast rts");
				}
				else
				{
					params.DisableRts();
					NS_LOG_DEBUG ("tx unicast");
				}
				params.DisableNextData();
				Low()->StartTransmission(m_currentPacket, &m_currentHdr, params,
						m_transmissionListener);
			}
		}
	}

}

void CtrlDcaTxop::NotifyInternalCollision(void)
{
	NS_LOG_FUNCTION (this);
	NotifyCollision();
}
void CtrlDcaTxop::NotifyCollision(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("collision");
	m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
	RestartAccessIfNeeded();
}

void CtrlDcaTxop::NotifyChannelSwitching(void)
{
	m_queue->Flush();
	m_currentPacket = 0;
}

void CtrlDcaTxop::GotCts(double snr, WifiMode txMode)
{
	NS_LOG_FUNCTION (this << snr << txMode);NS_LOG_DEBUG ("got cts");
}
void CtrlDcaTxop::MissedCts(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("missed cts");
	if (!NeedRtsRetransmission())
	{
		NS_LOG_DEBUG ("Cts Fail");
		m_stationManager->ReportFinalRtsFailed(m_currentHdr.GetAddr1(),
				&m_currentHdr);
		if (!m_txFailedCallback.IsNull())
		{
			m_txFailedCallback(m_currentHdr);
		}
		// to reset the dcf.
		m_currentPacket = 0;
		m_dcf->ResetCw();
	}
	else
	{
		m_dcf->UpdateFailedCw();
	}
	m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
	RestartAccessIfNeeded();
}
void CtrlDcaTxop::GotAck(double snr, WifiMode txMode)
{
	NS_LOG_FUNCTION (this << snr << txMode);
	if (!NeedFragmentation() || IsLastFragment())
	{
		NS_LOG_DEBUG ("got ack. tx done.");
		if (!m_txOkCallback.IsNull())
		{
			m_txOkCallback(m_currentHdr);
		}

		/* we are not fragmenting or we are done fragmenting
		 * so we can get rid of that packet now.
		 */
		m_currentPacket = 0;
		m_dcf->ResetCw();
		m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
		RestartAccessIfNeeded();
	}
	else
	{
		NS_LOG_DEBUG ("got ack. tx not done, size=" << m_currentPacket->GetSize ());
	}
}
void CtrlDcaTxop::MissedAck(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("missed ack");
	if (!NeedDataRetransmission())
	{
		NS_LOG_DEBUG ("Ack Fail");
		m_stationManager->ReportFinalDataFailed(m_currentHdr.GetAddr1(),
				&m_currentHdr);
		if (!m_txFailedCallback.IsNull())
		{
			m_txFailedCallback(m_currentHdr);
		}
		// to reset the dcf.
		m_currentPacket = 0;
		m_dcf->ResetCw();
	}
	else
	{
		NS_LOG_DEBUG ("Retransmit");
		m_currentHdr.SetRetry();
		m_dcf->UpdateFailedCw();
	}
	m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
	RestartAccessIfNeeded();
}
void CtrlDcaTxop::StartNext(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("start next packet fragment");
	/* this callback is used only for fragments. */
	NextFragment();
	WifiMacHeader hdr;
	Ptr<Packet> fragment = GetFragmentPacket(&hdr);
	MacLowTransmissionParameters params;
	params.EnableAck();
	params.DisableRts();
	params.DisableOverrideDurationId();
	if (IsLastFragment())
	{
		params.DisableNextData();
	}
	else
	{
		params.EnableNextData(GetNextFragmentSize());
	}
	Low()->StartTransmission(fragment, &hdr, params, m_transmissionListener);
}

void CtrlDcaTxop::Cancel(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("transmission cancelled");
	/**
	 * This happens in only one case: in an AP, you have two CtrlDcaTxop:
	 *   - one is used exclusively for beacons and has a high priority.
	 *   - the other is used for everything else and has a normal
	 *     priority.
	 *
	 * If the normal queue tries to send a unicast data frame, but
	 * if the tx fails (ack timeout), it starts a backoff. If the beacon
	 * queue gets a tx oportunity during this backoff, it will trigger
	 * a call to this Cancel function.
	 *
	 * Since we are already doing a backoff, we will get access to
	 * the medium when we can, we have nothing to do here. We just
	 * ignore the cancel event and wait until we are given again a
	 * tx oportunity.
	 *
	 * Note that this is really non-trivial because each of these
	 * frames is assigned a sequence number from the same sequence
	 * counter (because this is a non-802.11e device) so, the scheme
	 * described here fails to ensure in-order delivery of frames
	 * at the receiving side. This, however, does not matter in
	 * this case because we assume that the receiving side does not
	 * update its <seq,ad> tupple for packets whose destination
	 * address is a broadcast address.
	 */
}

void CtrlDcaTxop::EndTxNoAck(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("a transmission that did not require an ACK just finished");
	m_currentPacket = 0;
	m_dcf->ResetCw();
	m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
	StartAccessIfNeeded();
}

void CtrlDcaTxop::SendByCtrlChannelImpl(Ptr<const Packet> packet,
		WifiMacHeader dataHdr)
{
	NS_LOG_ERROR( (dataHdr.IsRts() ? "Sending RTS, dst = " : "Sending CTS, dst = ")
			    <<dataHdr.GetAddr1()
				<<", src="<<dataHdr.GetAddr2()
				<<", duration="<<dataHdr.GetDuration() );

	// find corresponding ctrl mac address
	Ptr<Node> dstNode;
	Mac48Address ctrlAddr;
	if (FindNode(dataHdr.GetAddr1(), dstNode))
	{
		ctrlAddr =
				DynamicCast<WifiNetDevice>(dstNode->GetDevice(1))->GetMac()->GetAddress();
	}
	else
	{
		NS_LOG_ERROR("unknown dest");
		return;
	}

	// translate header (data mac address -> ctrl mac address)
	WifiMacHeader ctrlHdr = dataHdr;
	if (dataHdr.IsRts()) // rts
	{
		ctrlHdr.SetAddr1(ctrlAddr);
		ctrlHdr.SetAddr2(m_low->GetAddress());
	}
	else if (dataHdr.IsCts())
	{
		ctrlHdr.SetAddr1(ctrlAddr);
	}
	else
	{
		NS_ASSERT(false);
		NS_LOG_ERROR("Error: Unsupported packet type");
	}

	Queue(packet, ctrlHdr);
}

void CtrlDcaTxop::SetNotifyDataChannelCallback(
		NotifyDataChannelCallback callback)
{
	NS_ASSERT(!callback.IsNull() );
	m_notifyDataChannelCallback = callback;
}

void CtrlDcaTxop::SetNodeContainer(NodeContainer *pContainer)
{
	m_pNodeContainer = pContainer;
}

bool CtrlDcaTxop::FindNode(Mac48Address addr, Ptr<Node> &node)
{
	bool bFound = false;

	if (0 == m_pNodeContainer)
	{
		NS_LOG_ERROR("m_pNodeContainer is null");
		return bFound;
	}

	Ptr<Node> tmpNode = 0;
	for (uint32_t i = 0; i < m_pNodeContainer->GetN(); ++i)
	{
		tmpNode = m_pNodeContainer->Get(i);

		// NOTE: we assume that index 0 is the ctrl NetDevice,
		//       while index 1 is the data NetDevice
		Mac48Address macAddrCtrl = DynamicCast<WifiNetDevice>(
				tmpNode->GetDevice(0))->GetMac()->GetAddress();
		Mac48Address macAddrData = DynamicCast<WifiNetDevice>(
				tmpNode->GetDevice(1))->GetMac()->GetAddress();
		if (macAddrCtrl == addr || macAddrData == addr)
		{
			bFound = true;
			break;
		}

	}

	if (bFound)
	{
		node = tmpNode;
	}

	return bFound;
}

void CtrlDcaTxop::NotifyDataChannel(Ptr<Packet> packet, WifiMacHeader hdr)
{
	if (m_notifyDataChannelCallback.IsNull())
	{
		NS_LOG_ERROR("m_notifyDataChannelCallback is null.");
		return;
	}

	// all the RTS/CTS need to translate addr1 to data address
	Ptr<Node> targetNode;
	Mac48Address dataAddr;
	if (FindNode(hdr.GetAddr1(), targetNode))
	{
		dataAddr =
				DynamicCast<WifiNetDevice>(targetNode->GetDevice(0))->GetMac()->GetAddress();
		hdr.SetAddr1(dataAddr);
	}
	else
	{
		NS_LOG_ERROR("unknown addr1");
		return;
	}

	if (hdr.IsRts()) // RTS need to translate addr2
	{
		if (FindNode(hdr.GetAddr2(), targetNode))
		{
			dataAddr =
					DynamicCast<WifiNetDevice>(targetNode->GetDevice(0))->GetMac()->GetAddress();
			hdr.SetAddr2(dataAddr);
		}
		else
		{
			NS_LOG_ERROR("unknown addr2");
			return;
		}

	}

	m_notifyDataChannelCallback(packet, hdr);
}

} // namespace ns3
