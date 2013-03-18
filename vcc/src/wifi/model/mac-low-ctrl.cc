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

#include "mac-low-ctrl.h"
#include "wifi-phy.h"
#include "wifi-mac-trailer.h"
#include "qos-utils.h"
#include "edca-txop-n.h"

NS_LOG_COMPONENT_DEFINE("MacLowCtrl");

#undef NS_LOG_APPEND_CONTEXT
#define NS_LOG_APPEND_CONTEXT std::clog << "[mac=" << m_self << "] "

namespace ns3
{

MacLowCtrl::MacLowCtrl()
{

}

MacLowCtrl::~MacLowCtrl()
{

}

void MacLowCtrl::StartTransmission(Ptr<const Packet> packet,
		const WifiMacHeader* hdr, MacLowTransmissionParameters params,
		MacLowTransmissionListener *listener)
{
	NS_LOG_FUNCTION (this << packet << hdr << params << listener);
	/* m_currentPacket is not NULL because someone started
	 * a transmission and was interrupted before one of:
	 *   - ctsTimeout
	 *   - sendDataAfterCTS
	 * expired. This means that one of these timers is still
	 * running. They are all cancelled below anyway by the
	 * call to CancelAllEvents (because of at least one
	 * of these two timer) which will trigger a call to the
	 * previous listener's cancel method.
	 *
	 * This typically happens because the high-priority
	 * QapScheduler has taken access to the channel from
	 * one of the Edca of the QAP.
	 */
	m_currentPacket = packet->Copy();
	m_currentHdr = *hdr;
	CancelAllEvents();
	m_listener = listener;
	m_txParams = params;

	//NS_ASSERT (m_phy->IsStateIdle ());

	NS_LOG_DEBUG ("startTx size=" << GetSize (m_currentPacket, &m_currentHdr) <<
			", to=" << m_currentHdr.GetAddr1 () << ", listener=" << m_listener);

	if (hdr->IsRts())
	{
		SendRtsPacket();
	}
	else if (hdr->IsCts())
	{
		SendCtsPacket();
	}
	else
	{
		MacLow::StartTransmission(packet, hdr, params, listener);
	}

	/* When this method completes, we have taken ownership of the medium. */
	NS_ASSERT(m_phy->IsStateTx ());
}

void MacLowCtrl::SendRtsPacket()
{
	StartDataTxTimers ();

	WifiMode rtsTxMode = GetRtsTxMode(m_currentPacket, &m_currentHdr);

	m_currentPacket->AddHeader(m_currentHdr);
	WifiMacTrailer fcs;
	m_currentPacket->AddTrailer(fcs);

	NS_LOG_ERROR("Sending Rts, dst="<<m_currentHdr.GetAddr1()
			<<", src="<<m_currentHdr.GetAddr2());
	ForwardDown(m_currentPacket, &m_currentHdr, rtsTxMode);
	m_currentPacket = 0;

}

void MacLowCtrl::SendCtsPacket()
{
	StartDataTxTimers ();

	WifiMode ctsTxMode = GetCtsTxModeForRts(m_currentHdr.GetAddr1(), m_lastRtsTxMode);

	m_currentPacket->AddHeader(m_currentHdr);
	WifiMacTrailer fcs;
	m_currentPacket->AddTrailer(fcs);

	SnrTag tag;
	tag.Set(m_lastSnr);
	m_currentPacket->AddPacketTag(tag);

	NS_LOG_ERROR("Sending Cts, dst="<<m_currentHdr.GetAddr1()
				<<", src="<<m_currentHdr.GetAddr2());
	ForwardDown(m_currentPacket, &m_currentHdr, ctsTxMode);
	m_currentPacket = 0;
}

void MacLowCtrl::ReceiveOk(Ptr<Packet> packet, double rxSnr, WifiMode txMode,
		WifiPreamble preamble)
{

	Ptr<Packet> myPacket = packet->Copy();
	WifiMacHeader myHdr;
	myPacket->RemoveHeader(myHdr);

	if (myHdr.IsRts() || myHdr.IsCts()) // notify data channel if it is RTS/CTS
	{
		Mac48Address addr = myHdr.IsRts() ? myHdr.GetAddr2() : myHdr.GetAddr1();
		m_stationManager->ReportRxOk(addr, &myHdr, rxSnr, txMode);

		//if this RTS is for me, then I have to remember the txMode and snr
		//since I will need if I decide to reply a Cts
		if (myHdr.IsRts() && myHdr.GetAddr1() == m_self)
		{
			m_lastRtsTxMode = txMode;
			m_lastSnr = rxSnr;
		}

		// notify data channel
		// NOTE: the header is removed!
		if (!m_notifyDataChannelCallback.IsNull())
		{
			m_notifyDataChannelCallback(myPacket, myHdr);
		}
		else
		{
			NS_LOG_ERROR("m_notifyDataChannelCallback is null.");
		}
	}
	else // give it to default processing
	{
		MacLow::ReceiveOk(packet, rxSnr, txMode, preamble);
	}

}

void MacLowCtrl::SetNotifyDataChannelCallback(
		NotifyDataChannelCallback callback)
{
	m_notifyDataChannelCallback = callback;
}

void MacLowCtrl::NotifyRxStartNow(Time rxDuration, WifiMacHeader hdr, Ptr<const Packet> packet)
{
	MacLow::NotifyRxStartNow(rxDuration, hdr, packet);
	if(hdr.IsRts() || hdr.IsCts() )
	{
		NS_LOG_ERROR(
				(hdr.IsRts() ? "Rxing Rts" : "Rxing Cts")
				<<", dst="<<hdr.GetAddr1()
				<<", src="<<hdr.GetAddr2()
				<<", rxing duration="<<rxDuration
					);
	}
}

void MacLowCtrl::NotifyTxStartNow(Time duration, WifiMacHeader hdr)
{
	MacLow::NotifyTxStartNow(duration, hdr);
}


} // namespace ns3
