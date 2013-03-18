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
#ifndef MAC_LOW_CTRL_H
#define MAC_LOW_CTRL_H

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

namespace ns3 {


/**
 * \ingroup wifi
 * \brief handle RTS/CTS/DATA/ACK transactions.
 */
class MacLowCtrl : public MacLow
{
public:

	typedef Callback <void, Ptr<Packet>, WifiMacHeader > NotifyDataChannelCallback;

  MacLowCtrl ();
  virtual ~MacLowCtrl ();

  void SendRtsPacket();
  void SendCtsPacket();

  virtual void StartTransmission (Ptr<const Packet> packet,
                          const WifiMacHeader* hdr,
                          MacLowTransmissionParameters parameters,
                          MacLowTransmissionListener *listener);

  virtual void ReceiveOk(Ptr<Packet> packet, double rxSnr, WifiMode txMode,
			WifiPreamble preamble);

  void SetNotifyDataChannelCallback(NotifyDataChannelCallback callback);

  virtual void NotifyRxStartNow(Time rxDuration, WifiMacHeader hdr, Ptr<const Packet> packet);
    virtual void NotifyTxStartNow(Time duration, WifiMacHeader hdr);

private:
  WifiMode m_lastRtsTxMode;
  double m_lastSnr;
  NotifyDataChannelCallback m_notifyDataChannelCallback;

};

} // namespace ns3

#endif /* MAC_LOW_H */
