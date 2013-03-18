/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005, 2006 INRIA
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
#ifndef MAC_LOW_DATA_H
#define MAC_LOW_DATA_H

#include <vector>
#include <stdint.h>
#include <ostream>
#include <map>

#include "wifi-mac-header.h"
#include "wifi-mode.h"
#include "wifi-preamble.h"
#include "wifi-remote-station-manager.h"
#include "ctrl-headers.h"
#include "mgt-headers.h"
#include "block-ack-agreement.h"
#include "ns3/mac48-address.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/packet.h"
#include "ns3/nstime.h"
#include "qos-utils.h"
#include "block-ack-cache.h"
#include "mac-low.h"

namespace ns3
{

/**
 * \ingroup wifi
 * \brief handle RTS/CTS/DATA/ACK transactions.
 */
class MacLowData: public MacLow
{
public:
	typedef Callback<void, Time, WifiMacHeader , Ptr<const Packet> > NotifyRxStartCallback;
	typedef Callback<void, Time, WifiMacHeader> NotifyTxStartCallback;

	MacLowData();
	virtual ~MacLowData();

	void SetupRtsForDataPacket(Ptr<const Packet> packet,
			const WifiMacHeader& hdr, Ptr<Packet> & rtsPacket,
			WifiMacHeader & rtsHdr, Time *pDelay);

	void SetupCtsForRts(Ptr<const Packet> packet, const WifiMacHeader& hdr,
			Ptr<Packet> & ctsPacket, WifiMacHeader & ctsHdr);

	void GotCtsPacket(Ptr<const Packet> packet, const WifiMacHeader & hdr);

	void ScheduleNav(Time &start, Time &duration);

	void UpdateNav(const Time & start, const Time & duration);

	virtual void ReceiveOk(Ptr<Packet> packet, double rxSnr, WifiMode txMode,
			WifiPreamble preamble);

	virtual void NotifyRxStartNow(Time rxDuration, WifiMacHeader hdr, Ptr<const Packet> packet);

	virtual void NotifyTxStartNow(Time duration, WifiMacHeader hdr);

	void SetNotifyRxStartCallback(NotifyRxStartCallback callback);

	void SetNotifyTxStartCallback(NotifyTxStartCallback callback);

	bool IsValidCts(Ptr<const Packet> ctsPacket, const WifiMacHeader& hdr);

private:
	NotifyRxStartCallback m_rxStartCallback;
	NotifyTxStartCallback m_txStartCallback;

};

} // namespace ns3

#endif /* MAC_LOW_H */
