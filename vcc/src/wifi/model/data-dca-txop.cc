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
#include "ns3/mobility-model.h"

#include "data-dca-txop.h"
#include "dcf-manager.h"
#include "mac-low.h"
#include "wifi-mac-queue.h"
#include "mac-tx-middle.h"
#include "wifi-mac-trailer.h"
#include "wifi-mac.h"
#include "random-stream.h"
#include "wifi-net-device.h"

NS_LOG_COMPONENT_DEFINE("DataDcaTxop");

#undef NS_LOG_APPEND_CONTEXT
#define NS_LOG_APPEND_CONTEXT if (m_low != 0) { std::clog << "[mac=" << m_low->GetAddress () << "] "; }

namespace ns3
{

class DataDcaTxop::Dcf: public DcfState
{
public:
	Dcf(DataDcaTxop * txop) :
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
	DataDcaTxop *m_txop;
};

class DataDcaTxop::TransmissionListener: public MacLowTransmissionListener
{
public:
	TransmissionListener(DataDcaTxop * txop) :
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
	DataDcaTxop *m_txop;
};

NS_OBJECT_ENSURE_REGISTERED(DataDcaTxop);

TypeId DataDcaTxop::GetTypeId(void)
{
	static TypeId tid = TypeId("ns3::DataDcaTxop").SetParent(
			ns3::Dcf::GetTypeId()).AddConstructor<DataDcaTxop>().AddAttribute(
			"Queue", "The WifiMacQueue object", PointerValue(),
			MakePointerAccessor(&DataDcaTxop::GetQueue),
			MakePointerChecker<WifiMacQueue>());
	return tid;
}

DataDcaTxop::DataDcaTxop() :
		m_manager(0), m_currentPacket(0)
{
	NS_LOG_FUNCTION (this);
	m_transmissionListener = new DataDcaTxop::TransmissionListener(this);
	m_dcf = new DataDcaTxop::Dcf(this);
	m_queue = CreateObject<WifiMacQueue>();
	m_rng = new RealRandomStream();
	m_txMiddle = new MacTxMiddle();
	m_bRequestAccessSucceeded = false;

	NS_LOG_INFO("This is dataDcaTxop");
}

DataDcaTxop::~DataDcaTxop()
{
	NS_LOG_FUNCTION (this);
}

void DataDcaTxop::DoDispose(void)
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

	m_bRequestAccessSucceeded = false;
}

void DataDcaTxop::SetManager(DcfManager *manager)
{
	NS_LOG_FUNCTION (this << manager);
	m_manager = manager;
	m_manager->Add(m_dcf);
}

void DataDcaTxop::SetLow(Ptr<MacLowData> low)
{
	NS_LOG_FUNCTION (this << low);
	m_low = low;

	m_low->SetNotifyRxStartCallback(
			MakeCallback(&DataDcaTxop::NotifyRxStart, this));
	m_low->SetNotifyTxStartCallback(
			MakeCallback(&DataDcaTxop::NotifyTxStart, this));
}
void DataDcaTxop::SetWifiRemoteStationManager(
		Ptr<WifiRemoteStationManager> remoteManager)
{
	NS_LOG_FUNCTION (this << remoteManager);
	m_stationManager = remoteManager;
}
void DataDcaTxop::SetTxOkCallback(TxOk callback)
{
	m_txOkCallback = callback;
}
void DataDcaTxop::SetTxFailedCallback(TxFailed callback)
{
	m_txFailedCallback = callback;
}

Ptr<WifiMacQueue> DataDcaTxop::GetQueue() const
{
	NS_LOG_FUNCTION (this);
	return m_queue;
}

void DataDcaTxop::SetMinCw(uint32_t minCw)
{
	NS_LOG_FUNCTION (this << minCw);
	m_dcf->SetCwMin(minCw);
}
void DataDcaTxop::SetMaxCw(uint32_t maxCw)
{
	NS_LOG_FUNCTION (this << maxCw);
	m_dcf->SetCwMax(maxCw);
}
void DataDcaTxop::SetAifsn(uint32_t aifsn)
{
	NS_LOG_FUNCTION (this << aifsn);
	m_dcf->SetAifsn(aifsn);
}
uint32_t DataDcaTxop::GetMinCw(void) const
{
	return m_dcf->GetCwMin();
}
uint32_t DataDcaTxop::GetMaxCw(void) const
{
	return m_dcf->GetCwMax();
}
uint32_t DataDcaTxop::GetAifsn(void) const
{
	return m_dcf->GetAifsn();
}

void DataDcaTxop::Queue(Ptr<const Packet> packet, const WifiMacHeader &hdr)
{
	NS_LOG_FUNCTION (this << packet << &hdr);

	if(NeedRts(packet, &hdr) )
	{
		++m_enqueueCount;
		NS_LOG_DEBUG("Data Channel enqueue, uid="<<packet->GetUid()
					<<", dst="<<hdr.GetAddr1()
					<<", size="<<packet->GetSize());
	}

	WifiMacTrailer fcs;
	uint32_t fullPacketSize = hdr.GetSerializedSize() + packet->GetSize()
			+ fcs.GetSerializedSize();
	m_stationManager->PrepareForQueue(hdr.GetAddr1(), &hdr, packet,
			fullPacketSize);
	m_queue->Enqueue(packet, hdr);

	StartAccessIfNeeded();
}

int64_t DataDcaTxop::AssignStreams(int64_t stream)
{
	NS_LOG_FUNCTION (this << stream);
	m_rng->AssignStreams(stream);
	return 1;
}

void DataDcaTxop::RestartAccessIfNeeded(void)
{
	NS_LOG_FUNCTION (this);

	if (m_currentPacket == 0 && m_bRequestAccessSucceeded) // already got a plan for current packet
	{
		// do nothing since we have already arranged sending for next packet
		NS_LOG_DEBUG("already arranged sending for next packet");
		return;
	}

	// Normal DCF
	if ((m_currentPacket != 0 || !m_queue->IsEmpty())
			&& !m_dcf->IsAccessRequested())
	{
		m_manager->RequestAccess(m_dcf);
	}

//	NS_LOG_ERROR( "Restart Access: "
//			    << (m_queue->IsEmpty() ? "empty queue, " : "unempty queue, ")
//				<< (m_bRequestedAccessByCtrlChannel?"requested":"Not requested")
//				);
//	StartAccessIfNeeded();

}

void DataDcaTxop::StartAccessIfNeeded(void)
{
	NS_LOG_FUNCTION (this);

	if (m_currentPacket == 0 && m_bRequestAccessSucceeded) // already got a plan for current packet
	{
		// do nothing since we have already arranged sending for next packet
		NS_LOG_DEBUG("already got a plan for next packet");
		return;
	}

	// normal DCF
	if (m_currentPacket == 0 && !m_queue->IsEmpty()
			&& !m_dcf->IsAccessRequested())
	{
		m_manager->RequestAccess(m_dcf);
	}

//	if (m_queue->IsEmpty())
//	{
//		return;
//	}
//
//	WifiMacHeader tmpHdr;
//	Ptr<const Packet> tmpPacket = m_queue->Peek(&tmpHdr);
//	if (NeedRts(tmpPacket, &tmpHdr))
//	{
//		if (!m_bRequestedAccessByCtrlChannel)
//		{
//			this->RequestAccessByCtrlChannel(tmpPacket, tmpHdr);
//		}
//
//	}
//	else
//	{
//		if (!m_dcf->IsAccessRequested())
//		{
//			NS_LOG_ERROR("Request Access by DCF for packet_"<< tmpHdr.GetSequenceNumber() );
//			m_manager->RequestAccess(m_dcf);
//		}
//	}
}

Ptr<MacLow> DataDcaTxop::Low(void)
{
	return m_low;
}

bool DataDcaTxop::NeedRts(Ptr<const Packet> packet, const WifiMacHeader *header)
{
	return m_stationManager->NeedRts(header->GetAddr1(), header, packet);
}

void DataDcaTxop::DoStart()
{
	m_dcf->ResetCw();
	m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
	ns3::Dcf::DoStart();
}
bool DataDcaTxop::NeedRtsRetransmission(void)
{
	return m_stationManager->NeedRtsRetransmission(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket);
}

bool DataDcaTxop::NeedDataRetransmission(void)
{
	return m_stationManager->NeedDataRetransmission(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket);
}
bool DataDcaTxop::NeedFragmentation(void)
{
	return m_stationManager->NeedFragmentation(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket);
}

void DataDcaTxop::NextFragment(void)
{
	m_fragmentNumber++;
}

uint32_t DataDcaTxop::GetFragmentSize(void)
{
	return m_stationManager->GetFragmentSize(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket, m_fragmentNumber);
}
bool DataDcaTxop::IsLastFragment(void)
{
	return m_stationManager->IsLastFragment(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket, m_fragmentNumber);
}

uint32_t DataDcaTxop::GetNextFragmentSize(void)
{
	return m_stationManager->GetFragmentSize(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket, m_fragmentNumber + 1);
}

uint32_t DataDcaTxop::GetFragmentOffset(void)
{
	return m_stationManager->GetFragmentOffset(m_currentHdr.GetAddr1(),
			&m_currentHdr, m_currentPacket, m_fragmentNumber);
}

Ptr<Packet> DataDcaTxop::GetFragmentPacket(WifiMacHeader *hdr)
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

bool DataDcaTxop::NeedsAccess(void) const
{
	return !m_queue->IsEmpty() || m_currentPacket != 0;
}
void DataDcaTxop::NotifyAccessGranted(void)
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
		uint16_t sequence = m_txMiddle->GetNextSequenceNumberfor(&m_currentHdr);
		m_currentHdr.SetSequenceNumber(sequence);
		m_currentHdr.SetFragmentNumber(0);
		m_currentHdr.SetNoMoreFragments();
		m_currentHdr.SetNoRetry();
		m_fragmentNumber = 0;
		NS_LOG_DEBUG ("dequeued size=" << m_currentPacket->GetSize () <<
				", to=" << m_currentHdr.GetAddr1 () <<
				", seq=" << m_currentHdr.GetSequenceControl ());
	}
	MacLowTransmissionParameters params;
	params.DisableOverrideDurationId();
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
				//params.EnableRts();
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
			params.DisableRts();
			Low()->StartTransmission(fragment, &hdr, params,
					m_transmissionListener);
		}
		else
		{
			if (NeedRts(m_currentPacket, &m_currentHdr))
			{
//				params.EnableRts(); //Since we have requested by Ctrl Channel, RTS is not needed.
				NS_LOG_DEBUG ("tx unicast rts");
			}
			else
			{
				params.DisableRts();
				NS_LOG_DEBUG ("tx unicast");
			}
			params.DisableRts();
			params.DisableNextData();
			Low()->StartTransmission(m_currentPacket, &m_currentHdr, params,
					m_transmissionListener);
		}
	}
}

void DataDcaTxop::NotifyInternalCollision(void)
{
	NS_LOG_FUNCTION (this);
	NotifyCollision();
}
void DataDcaTxop::NotifyCollision(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("collision");
	m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
	RestartAccessIfNeeded();
}

void DataDcaTxop::NotifyChannelSwitching(void)
{
	m_queue->Flush();
	m_currentPacket = 0;
}

void DataDcaTxop::GotCts(double snr, WifiMode txMode)
{
	NS_LOG_FUNCTION (this << snr << txMode);NS_LOG_DEBUG ("got cts");
}
void DataDcaTxop::MissedCts(void)
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
void DataDcaTxop::GotAck(double snr, WifiMode txMode)
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
void DataDcaTxop::MissedAck(void)
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
void DataDcaTxop::StartNext(void)
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

void DataDcaTxop::Cancel(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("transmission cancelled");
	/**
	 * This happens in only one case: in an AP, you have two DataDcaTxop:
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

void DataDcaTxop::EndTxNoAck(void)
{
	NS_LOG_FUNCTION (this);NS_LOG_DEBUG ("a transmission that did not require an ACK just finished");
	m_currentPacket = 0;
	m_dcf->ResetCw();
	m_dcf->StartBackoffNow(m_rng->GetNext(0, m_dcf->GetCw()));
	StartAccessIfNeeded();
}

void DataDcaTxop::SetSendByCtrlChannleCallback(
		SendByCtrlChannelCallback callback)
{
	m_sendByCtrlChannelCallback = callback;
}
// request ctrl channel to send RTS
void DataDcaTxop::RequestAccessByCtrlChannel(Ptr<const Packet> packet,
		const WifiMacHeader &hdr)
{
	if (m_sendByCtrlChannelCallback.IsNull())
	{
		NS_LOG_ERROR("m_sendByCtrlChannelCallback is null.");
		return;
	}

	if(!(*m_pbGlobalFirstRts))
	{
		NS_LOG_ERROR("This is NOT the first Rts in current rxing, cancel it");
		return;
	}

	*m_pbGlobalFirstRts = false;
	// reset global RTS signal after this rxing
	Time firtRtsResetDelay = m_low->m_myLastRxStart + m_low->m_myLastRxDuration - Simulator::Now();
	NS_ASSERT(firtRtsResetDelay.IsStrictlyPositive() );
	Simulator::Cancel(m_firtRtsReset);
	m_firtRtsReset = Simulator::Schedule(firtRtsResetDelay,
			&DataDcaTxop::ResetGlobalFirstRts, this);

	NS_LOG_ERROR("Prepare to send Rts for next packet, pkt uid="<< packet->GetUid() );
	// prepare RTS
	Time delay;
	Ptr<Packet> rtsPacket = Create<Packet>();
	WifiMacHeader rtsHdr;
	m_low->SetupRtsForDataPacket(packet, hdr, rtsPacket, rtsHdr, &delay);

	m_sendByCtrlChannelCallback(rtsPacket, rtsHdr);

	// We do NOT need it any more, since we don't re-send Rts
//	// setup CTS timeout event
//	Simulator::Cancel(m_ctrlRtsTimeout);
//	Time lastNavEnd = m_low->m_lastNavStart + m_low->m_lastNavDuration;
//	Time rtsTimeoutValue = std::max(lastNavEnd, Simulator::Now() + delay)
//			- Simulator::Now();
//	NS_LOG_ERROR("Set ctrl Rts Timeout = "<<rtsTimeoutValue);
//	m_ctrlRtsTimeout = Simulator::Schedule(rtsTimeoutValue,
//			&DataDcaTxop::CtrlRtsTimeout, this);

}

//TODO: Currently, when the CTS timeout,
//      it just send an RTS by using ctrl channel again.
void DataDcaTxop::CtrlRtsTimeout()
{
//	NS_LOG_ERROR("Rts on ctrl channel Timeout, restart access if needed");
//	m_bRequestedAccessByCtrlChannel = false; //last request is out of date
//	this->RestartAccessIfNeeded();
}

// Got a packet from ctrl channel
void DataDcaTxop::NotifyDataChannelImpl(Ptr<Packet> packet, WifiMacHeader hdr)
{
	if (hdr.IsRts() && hdr.GetAddr1() == m_low->m_self) // RTS for me
	{
		NS_LOG_ERROR("Data Channel Got RTS: dst=" <<hdr.GetAddr1()
				<<", src="<<hdr.GetAddr2()
				<<", duration="<<hdr.GetDuration() );

		// setup CTS packet
		Ptr<Packet> ctsPacket;
		WifiMacHeader ctsHdr;
		m_low->SetupCtsForRts(packet, hdr, ctsPacket, ctsHdr);

		// send CTS by ctrl channel
		if (!m_sendByCtrlChannelCallback.IsNull())
		{
			m_sendByCtrlChannelCallback(ctsPacket, ctsHdr);
		}
		else
		{
			NS_LOG_ERROR("m_sendByCtrlChannelCallback is null");
		}

	}
	else if (hdr.IsCts()) // all the CTS must be processed
	{
		if (!m_low->IsValidCts(packet, hdr))
		{
			NS_LOG_ERROR("Invalid Cts, drop it.");
			return;
		}

		// give all CTS to MacLow to update its NAV
		m_low->GotCtsPacket(packet, hdr);

		if (hdr.GetAddr1() == m_low->m_self) // CTS for me
		{
//			// cancel CTS timeout event
//			Simulator::Cancel(m_ctrlRtsTimeout);

			m_bRequestAccessSucceeded = true;

			// send data according CTS
			uint64_t startTime;
			packet->CopyData((uint8_t *) (&startTime), sizeof(uint64_t));
			Time start(startTime);

			// NOTE: here we have to wait for a SIFS before sending data!
			Time delay = start + m_low->GetSifs() - Simulator::Now();
			NS_ASSERT(delay.IsStrictlyPositive());

			EventId sendPacketId = Simulator::Schedule(delay,
					&DataDcaTxop::SendPacketAsScheduled, this);
			NS_LOG_ERROR("prepare to send data after CTS, delay = " << delay
					<<", EventId = "<<sendPacketId.GetUid());

		}
	}
}

void DataDcaTxop::SetGlobalRtsSignal(bool *pGlobalSignal)
{
	m_pbGlobalFirstRts = pGlobalSignal;
}

void DataDcaTxop::ResetGlobalFirstRts()
{
	*m_pbGlobalFirstRts = true;
}

void DataDcaTxop::SendPacketAsScheduled()
{
	NS_ASSERT(m_bRequestAccessSucceeded);

	if (m_currentPacket == 0)
	{
		if (m_queue->IsEmpty())
		{
			NS_LOG_DEBUG ("queue empty");
			return;
		}
		m_currentPacket = m_queue->Dequeue(&m_currentHdr);
		NS_ASSERT(m_currentPacket != 0);
		uint16_t sequence = m_txMiddle->GetNextSequenceNumberfor(&m_currentHdr);
		m_currentHdr.SetSequenceNumber(sequence);
		m_currentHdr.SetFragmentNumber(0);
		m_currentHdr.SetNoMoreFragments();
		m_currentHdr.SetNoRetry();
		m_fragmentNumber = 0;
		NS_LOG_DEBUG ("dequeued size=" << m_currentPacket->GetSize () <<
				", to=" << m_currentHdr.GetAddr1 () <<
				", seq=" << m_currentHdr.GetSequenceControl ());
	}

	NS_LOG_ERROR("Sending Data af. Cts, uid="<<m_currentPacket->GetUid()
			<<", size = "<< m_currentPacket->GetSize()
			<<", dst = "<< m_currentHdr.GetAddr1()
			<<", src = "<< m_currentHdr.GetAddr2()
			<<", target = "<< m_currentHdr.GetAddr3());

	MacLowTransmissionParameters params;
	params.DisableOverrideDurationId();

	if (m_currentHdr.GetAddr1().IsGroup())
	{
		params.DisableAck();
		params.DisableNextData();

		params.DisableRts();
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
				//params.EnableRts();
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
			params.DisableRts();
			Low()->StartTransmission(fragment, &hdr, params,
					m_transmissionListener);
		}
		else
		{
			params.DisableRts();
			params.DisableNextData();
			Low()->StartTransmission(m_currentPacket, &m_currentHdr, params,
					m_transmissionListener);
		}
	}

	m_bRequestAccessSucceeded = false;

}

// someone is transmitting
void DataDcaTxop::NotifyRxStart(Time rxDuration, WifiMacHeader hdr, Ptr<const Packet> packet)
{
	// the current on-air packet is data and long enough, send an Rts
	// on ctrl channel if needed
	if (hdr.IsData() && rxDuration > Time(180000) )
	{
		NS_LOG_DEBUG("start Rx data, src="<<hdr.GetAddr2()
				<< ", dst="<<hdr.GetAddr1()
				<< ", uid="<<packet->GetUid()
				<< ", size="<<packet->GetSize()
				<<", duration="<<rxDuration);
		Time delay = NanoSeconds(20); // wait for a while to make sure every node has began to receive current on-air packet
		Simulator::Schedule(delay, &DataDcaTxop::SendRtsOnCtrlChannelIfNeeded,
				this, rxDuration, hdr);
	}

}

void DataDcaTxop::NotifyTxStart(Time duration, WifiMacHeader hdr)
{
	// We do not consider a node transmits data and control simultaneously
	// so, we don't do anything here
	if (hdr.IsData() && duration > Time(180000))
	{
		NS_LOG_ERROR("start Tx data, src="<<hdr.GetAddr2()
				<< ", dst="<<hdr.GetAddr1()
				<<", duration="<<duration);
	}
}

void DataDcaTxop::SendRtsOnCtrlChannelIfNeeded(Time duration,
		WifiMacHeader onAirHdr)
{
	// check whether to send RTS on ctrl channel
	if (m_queue->IsEmpty())
	{
		NS_LOG_INFO("do NOT need Rts since m_queue is empty.");
		return;
	}

	WifiMacHeader nextHdr;
	Ptr<const Packet> nextPacket = m_queue->Peek(&nextHdr);

	if (
			nextHdr.IsData() && NeedRts(nextPacket, &nextHdr)
			&& IsEnoughForCtrlRts(nextHdr.GetAddr1(), m_low->m_self,
					onAirHdr.GetAddr1(), onAirHdr.GetAddr2() )
		)
	{
		RequestAccessByCtrlChannel(nextPacket, nextHdr);
	}
	else
	{
		NS_LOG_DEBUG("No need to send Rts: "
				<< (nextHdr.IsData() ? "Data, " : "Not Data, ")
				<< (NeedRts(nextPacket, &nextHdr) ? "Need Rts, " : "No Rts, ")
		);
	}

}

bool DataDcaTxop::FindNode(Mac48Address addr, Ptr<Node> &node)
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

void DataDcaTxop::SetNodeContainer(NodeContainer *pContainer)
{
	m_pNodeContainer = pContainer;
}

double DataDcaTxop::CalculateDistance(Ptr<Node> node1, Ptr<Node> node2)
{
	double dis = 0.0;

	Ptr<MobilityModel> mob1 = node1->GetObject<MobilityModel>();
	Vector pos1 = mob1->GetPosition();

	Ptr<MobilityModel> mob2 = node2->GetObject<MobilityModel>();
	Vector pos2 = mob2->GetPosition();

	dis = std::pow((pos1.x - pos2.x), 2.0) + std::pow((pos1.y - pos2.y), 2.0)
			+ std::pow((pos1.z - pos2.z), 2.0);

	dis = std::sqrt(dis);

	return dis;
}

// condition:
// 1. if a node is sending or receive its packet, it is not allowed to send RTS/CTS simultaneously
// 2. if the distance btw rts sender and on-air packet receiver is large than 1 meter,
//    then send the Rts on ctrl channel
bool DataDcaTxop::IsEnoughForCtrlRts(Mac48Address rtsDst, Mac48Address rtsSrc,
		Mac48Address onAirDst, Mac48Address onAirSrc)
{
	if (rtsDst == onAirDst)		// condition 1
	{
		NS_LOG_ERROR("do not allow Rts with dst == onAirDst");
		return false;
	}

	if (rtsDst == onAirSrc)		// condition 1
	{
		NS_LOG_ERROR("do not allow Rts with dst == onAirSrc");
		return false;
	}

	Ptr<Node> dstNode;
	if (!FindNode(onAirDst, dstNode))
	{
		NS_LOG_ERROR("Can NOT find corresponding node for onAirDst");
		return false;
	}

	Ptr<Node> myNode;
	if (!FindNode(rtsSrc, myNode))
	{
		NS_LOG_ERROR("Can NOT find corresponding node for rtsSrc");
		return false;
	}

	double dis = CalculateDistance(myNode, dstNode);
	if (dis >= 1.0)
	{
		return true;
	}
	else
	{
		NS_LOG_ERROR("Too close to send Rts");
		return false;
	}

	return false;

}

void DataDcaTxop::GetDataChannelStateImpl(Time & lastRxStart,
		Time & lastRxDuration, Time & lastTxStart, Time & lastTxDuration,
		Time & lastNavStart, Time & lastNavDuration)
{
	lastRxStart = m_low->m_myLastRxStart;
	lastRxDuration = m_low->m_myLastRxDuration;

	lastTxStart = m_low->m_myLastTxStart;
	lastTxDuration = m_low->m_myLastTxDuration;

	lastNavStart = m_low->m_lastNavStart;
	lastNavDuration = m_low->m_lastNavDuration;
}

} // namespace ns3
