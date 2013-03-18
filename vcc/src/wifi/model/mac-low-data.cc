/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005,2006 INRIA
 * Copyright (c) 2009 MIRKO BANCHI
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
 * Author: Mirko Banchi <mk.banchi@gmail.com>
 */

#include "ns3/assert.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/tag.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/double.h"

#include "mac-low-data.h"
#include "wifi-phy.h"
#include "wifi-mac-trailer.h"
#include "qos-utils.h"
#include "edca-txop-n.h"

NS_LOG_COMPONENT_DEFINE("MacLowData");

#undef NS_LOG_APPEND_CONTEXT
#define NS_LOG_APPEND_CONTEXT std::clog << "[mac=" << m_self << "] "

namespace ns3
{

MacLowData::MacLowData()
{

}

MacLowData::~MacLowData()
{

}

//  NOTE: 1. the output packet have NOT include the header
//	      2. As the Cts is not transmitted on data channel, so
//	         we have NOT included the CtsTxTime and the SIFS before
//	         replying Cts in the duration field of Rts
void MacLowData::SetupRtsForDataPacket(Ptr<const Packet> packet,
		const WifiMacHeader& hdr, Ptr<Packet> & rtsPacket,
		WifiMacHeader & rtsHdr, Time *pDelay)
{
	NS_LOG_FUNCTION (this);

	/* Setup an RTS for this packet. */
	rtsHdr.SetType(WIFI_MAC_CTL_RTS);
	rtsHdr.SetDsNotFrom();
	rtsHdr.SetDsNotTo();
	rtsHdr.SetNoRetry();
	rtsHdr.SetNoMoreFragments();
	rtsHdr.SetAddr1(hdr.GetAddr1());
	rtsHdr.SetAddr2(m_self);
//	rts.SetAddr3(hdr.GetAddr3());
	WifiMode rtsTxMode = GetRtsTxMode(packet, &hdr);
	Time duration = Seconds(0);

	WifiMode dataTxMode = GetDataTxMode(packet, &hdr);
//	duration += GetSifs(); // this SIFS is the waiting time before replying CTS, we don't need it any more.
//	duration += GetCtsDuration(hdr.GetAddr1(), rtsTxMode); // As the CTS is NOT transmitted on the data channel, do NOT need any more
	duration += GetSifs();
	duration += m_phy->CalculateTxDuration(GetSize(packet, &hdr), dataTxMode,
			WIFI_PREAMBLE_LONG);
	duration += GetSifs();
	duration += GetAckDuration(hdr.GetAddr1(), dataTxMode);

	rtsHdr.SetDuration(duration);

	// TODO: Now, we don't consider the re-transmission of Rts
	Time txDuration = m_phy->CalculateTxDuration(GetRtsSize(), rtsTxMode,
			WIFI_PREAMBLE_LONG);
	*pDelay = txDuration + GetCtsTimeout() + GetSifs();
}

// create a CTS packet according to RTS packet
void MacLowData::SetupCtsForRts(Ptr<const Packet> rtsPacket,
		const WifiMacHeader& rtsHdr, Ptr<Packet> & ctsPacket,
		WifiMacHeader & ctsHdr)
{

	// Get schedule according to the Rts
	Time start = MicroSeconds(0);
	Time duration = rtsHdr.GetDuration();
	ScheduleNav(start, duration);

	ctsHdr.SetType(WIFI_MAC_CTL_CTS);
	ctsHdr.SetDsNotFrom();
	ctsHdr.SetDsNotTo();
	ctsHdr.SetNoMoreFragments();
	ctsHdr.SetNoRetry();
	ctsHdr.SetAddr1(rtsHdr.GetAddr2());
	ctsHdr.SetDuration(duration);

	uint64_t startTime = start.GetInteger();
	ctsPacket = Create<Packet>((uint8_t *) (&startTime), sizeof(uint64_t));
}

void MacLowData::ScheduleNav(Time &start, Time & duration)
{
	Time now = Simulator::Now();

	NS_LOG_ERROR("Try to schedule a NAV: "
			<<"start = " <<start
			<<", duration = "<<duration
			<<", NavStart = "<<m_lastNavStart
			<<", NavDuration = "<<m_lastNavDuration
			<<", RxStart = " << m_myLastRxStart
			<<", RxDuration = " << m_myLastRxDuration
			<<", TxStart = " << m_myLastTxStart
			<<", TxDuration = " << m_myLastTxDuration);

	Time lastNavEnd = m_lastNavStart + m_lastNavDuration;
	Time lastTxEnd = m_myLastTxStart + m_myLastTxDuration;
	Time lastRxEnd = m_myLastRxStart + m_myLastRxDuration;

	start = std::max(lastNavEnd, std::max(lastTxEnd, lastRxEnd));
	NS_ASSERT(start > now);
	Time end = start + duration;
	Time realDuration = end - now;

	for (DcfListenersCI i = m_dcfListeners.begin(); i != m_dcfListeners.end();
			i++)
	{
		(*i)->NavStart(realDuration);
	}

	m_lastNavStart = now;
	m_lastNavDuration = realDuration;

	NS_LOG_ERROR("new NAV: start = " << now << ", duration = " << realDuration);

}

void MacLowData::UpdateNav(const Time & start, const Time & duration)
{
	Time lastNavEnd = m_lastNavStart + m_lastNavDuration;
	Time newNavEnd = start + duration;
	if (newNavEnd > lastNavEnd)
	{
		Time now = Simulator::Now();
		Time realDuration = newNavEnd - now;
		for (DcfListenersCI i = m_dcfListeners.begin();
				i != m_dcfListeners.end(); i++)
		{
			(*i)->NavStart(realDuration);
		}

		m_lastNavStart = now;
		m_lastNavDuration = realDuration;

		NS_LOG_DEBUG("Nav updated: start = " << now << ", duration = " << realDuration);
	}
	else
	{
		NS_LOG_ERROR("Nav doesn't change");
	}
}

void MacLowData::GotCtsPacket(Ptr<const Packet> packet,
		const WifiMacHeader & hdr)
{
	Time duration = hdr.GetDuration();

	uint64_t startTime;
	packet->CopyData((uint8_t *) (&startTime), sizeof(uint64_t));
	Time start(startTime);

	NS_LOG_ERROR("Got CTS: dst=" <<hdr.GetAddr1()
			<<", src="<<hdr.GetAddr2()
			<<", duration="<<hdr.GetDuration()
			<<", start="<<start );

	UpdateNav(start, duration);

}

void MacLowData::ReceiveOk(Ptr<Packet> packet, double rxSnr, WifiMode txMode,
		WifiPreamble preamble)
{
	WifiMacHeader hdr;
	packet->PeekHeader(hdr);
	if (hdr.GetAddr1() == m_self && hdr.IsData() &&  packet->GetSize() > 512)
	{
		NS_LOG_ERROR ("Receive data OK, src = " << hdr.GetAddr2 ()
				<<", size = " << packet->GetSize() );
	}

	MacLow::ReceiveOk(packet, rxSnr, txMode, preamble);
}

void MacLowData::NotifyRxStartNow(Time rxDuration, WifiMacHeader hdr, Ptr<const Packet> packet)
{
	// Note: we must call MacLow::NotifyRxStartNow() here!
	MacLow::NotifyRxStartNow(rxDuration, hdr, packet);

	if (m_rxStartCallback.IsNull())
	{
		NS_LOG_ERROR("Error: m_rxStartCallback is null");
		return;
	}

	m_rxStartCallback(rxDuration, hdr, packet);
}

void MacLowData::NotifyTxStartNow(Time duration, WifiMacHeader hdr)
{
	// Note: we must call MacLow::NotifyTxStartNow() here!
	MacLow::NotifyTxStartNow(duration, hdr);

	if (m_txStartCallback.IsNull())
	{
		NS_LOG_ERROR("Error: m_txStartCallback is null");
		return;
	}

	m_txStartCallback(duration, hdr);
}

void MacLowData::SetNotifyRxStartCallback(NotifyRxStartCallback callback)
{
	m_rxStartCallback = callback;
}

void MacLowData::SetNotifyTxStartCallback(NotifyTxStartCallback callback)
{
	m_txStartCallback = callback;
}

bool MacLowData::IsValidCts(Ptr<const Packet> ctsPacket,
		const WifiMacHeader& hdr)
{
	uint64_t startTime;
	ctsPacket->CopyData((uint8_t *) (&startTime), sizeof(uint64_t));
	Time start(startTime);
	start = start + GetSifs();//the real start time to send next packet

	Time navEnd = m_lastNavStart + m_lastNavDuration;
	Time txEnd = m_myLastTxStart + m_myLastTxDuration;
	Time rxEnd = m_myLastRxStart + m_myLastRxDuration;

	if (start <= navEnd)
	{
		NS_LOG_ERROR("Invalid Cts, start="<<start<<", navEnd="<<navEnd);
		return false;
	}

	if (start <= txEnd)
	{
		NS_LOG_ERROR("Invalid Cts, start="<<start<<", txEnd="<<txEnd);
		return false;
	}

	if (start <= rxEnd)
	{
		NS_LOG_ERROR("Invalid Cts, start="<<start<<", rxEnd="<<rxEnd);
		return false;
	}

	return true;
}

} // namespace ns3
