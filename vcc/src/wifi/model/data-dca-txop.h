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

#ifndef DATA_DCA_TXOP_H
#define DATA_DCA_TXOP_H

#include <stdint.h>
#include "ns3/callback.h"
#include "ns3/packet.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-mode.h"
#include "ns3/wifi-remote-station-manager.h"
#include "ns3/dcf.h"
#include "ns3/mac-low-data.h"
#include "ns3/node-container.h"

namespace ns3 {

class DcfState;
class DcfManager;
class WifiMacQueue;
class MacLow;
class WifiMacParameters;
class MacTxMiddle;
class RandomStream;
class MacStation;
class MacStations;

/**
 * \brief handle packet fragmentation and retransmissions.
 * \ingroup wifi
 *
 * This class implements the packet fragmentation and
 * retransmission policy. It uses the ns3::MacLow and ns3::DcfManager
 * helper classes to respectively send packets and decide when
 * to send them. Packets are stored in a ns3::WifiMacQueue until
 * they can be sent.
 *
 * The policy currently implemented uses a simple fragmentation
 * threshold: any packet bigger than this threshold is fragmented
 * in fragments whose size is smaller than the threshold.
 *
 * The retransmission policy is also very simple: every packet is
 * retransmitted until it is either successfully transmitted or
 * it has been retransmitted up until the ssrc or slrc thresholds.
 *
 * The rts/cts policy is similar to the fragmentation policy: when
 * a packet is bigger than a threshold, the rts/cts protocol is used.
 */
class DataDcaTxop : public Dcf
{
public:
  static TypeId GetTypeId (void);

  typedef Callback <void, const WifiMacHeader&> TxOk;
  typedef Callback <void, const WifiMacHeader&> TxFailed;
  typedef Callback <void, Ptr<const Packet>, WifiMacHeader> SendByCtrlChannelCallback;

  DataDcaTxop ();
  ~DataDcaTxop ();

  void SetLow (Ptr<MacLowData> low);
  void SetManager (DcfManager *manager);
  void SetWifiRemoteStationManager (Ptr<WifiRemoteStationManager> remoteManager);

  /**
   * \param callback the callback to invoke when a
   * packet transmission was completed successfully.
   */
  void SetTxOkCallback (TxOk callback);
  /**
   * \param callback the callback to invoke when a
   * packet transmission was completed unsuccessfully.
   */
  void SetTxFailedCallback (TxFailed callback);

  Ptr<WifiMacQueue > GetQueue () const;
  virtual void SetMinCw (uint32_t minCw);
  virtual void SetMaxCw (uint32_t maxCw);
  virtual void SetAifsn (uint32_t aifsn);
  virtual uint32_t GetMinCw (void) const;
  virtual uint32_t GetMaxCw (void) const;
  virtual uint32_t GetAifsn (void) const;

  /**
   * \param packet packet to send
   * \param hdr header of packet to send.
   *
   * Store the packet in the internal queue until it
   * can be sent safely.
   */
  void Queue (Ptr<const Packet> packet, const WifiMacHeader &hdr);

 /**
  * Assign a fixed random variable stream number to the random variables
  * used by this model.  Return the number of streams (possibly zero) that
  * have been assigned.
  *
  * \param stream first stream index to use
  * \return the number of stream indices assigned by this model
  */
  int64_t AssignStreams (int64_t stream);

  Ptr<MacLow> Low (void);

private:
  class TransmissionListener;
  class NavListener;
  class PhyListener;
  class Dcf;
  friend class Dcf;
  friend class TransmissionListener;

  DataDcaTxop &operator = (const DataDcaTxop &);
  DataDcaTxop (const DataDcaTxop &o);

  // Inherited from ns3::Object

  void DoStart ();
  /* dcf notifications forwarded here */
  bool NeedsAccess (void) const;
  void NotifyAccessGranted (void);
  void NotifyInternalCollision (void);
  void NotifyCollision (void);
  /**
  * When a channel switching occurs, enqueued packets are removed.
  */
  void NotifyChannelSwitching (void);
  /* event handlers */
  void GotCts (double snr, WifiMode txMode);
  void MissedCts (void);
  void GotAck (double snr, WifiMode txMode);
  void MissedAck (void);
  void StartNext (void);
  void Cancel (void);
  void EndTxNoAck (void);

  void RestartAccessIfNeeded (void);
  void StartAccessIfNeeded (void);
  bool NeedRts (Ptr<const Packet> packet, const WifiMacHeader *header);
  bool NeedRtsRetransmission (void);
  bool NeedDataRetransmission (void);
  bool NeedFragmentation (void);
  uint32_t GetNextFragmentSize (void);
  uint32_t GetFragmentSize (void);
  uint32_t GetFragmentOffset (void);
  bool IsLastFragment (void);
  void NextFragment (void);
  Ptr<Packet> GetFragmentPacket (WifiMacHeader *hdr);
  virtual void DoDispose (void);

  Dcf *m_dcf;
  DcfManager *m_manager;
  TxOk m_txOkCallback;
  TxFailed m_txFailedCallback;
  Ptr<WifiMacQueue> m_queue;
  MacTxMiddle *m_txMiddle;
  Ptr <MacLowData> m_low;
  Ptr<WifiRemoteStationManager> m_stationManager;
  TransmissionListener *m_transmissionListener;
  RandomStream *m_rng;

  bool m_accessOngoing;
  Ptr<const Packet> m_currentPacket;
  WifiMacHeader m_currentHdr;
  uint8_t m_fragmentNumber;



public:
  void NotifyDataChannelImpl(Ptr<Packet> packet, WifiMacHeader hdr);

  void SetSendByCtrlChannleCallback(SendByCtrlChannelCallback callback);

  void SetGlobalRtsSignal(bool *pGlobalSignal);

  void RequestAccessByCtrlChannel(Ptr<const Packet> packet, const WifiMacHeader &hdr);

  void CtrlRtsTimeout();

  void ResetGlobalFirstRts();

  void SendPacketAsScheduled();

  void NotifyRxStart(Time rxDuration, WifiMacHeader hdr, Ptr<const Packet> packet);

  void NotifyTxStart(Time duration, WifiMacHeader hdr);

  void SendRtsOnCtrlChannelIfNeeded(Time duration, WifiMacHeader onAirHdr);

  bool FindNode(Mac48Address addr, Ptr<Node> &node);

  void SetNodeContainer(NodeContainer *pContainer);

  double CalculateDistance(Ptr<Node> node1, Ptr<Node> node2);

  bool IsEnoughForCtrlRts(Mac48Address rtsDst, Mac48Address rtsSrc, Mac48Address onAirDst, Mac48Address onAirSrc);

  void GetDataChannelStateImpl(Time & lastRxStart, Time & lastRxDuration,
			Time & lastTxStart, Time & lastTxDuration, Time & lastNavStart,
			Time & lastNavDuration);

private:
  SendByCtrlChannelCallback m_sendByCtrlChannelCallback;
  bool m_bRequestAccessSucceeded;
  EventId m_ctrlRtsTimeout;
  EventId m_firtRtsReset;
  bool *m_pbGlobalFirstRts;
  uint16_t m_packetID;

  NodeContainer * m_pNodeContainer;

public:
  uint32_t m_enqueueCount;
};

} // namespace ns3



#endif /* DATA_DCA_TXOP_H */
