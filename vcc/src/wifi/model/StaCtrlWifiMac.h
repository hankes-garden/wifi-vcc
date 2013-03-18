/*
 * StaCtrlWifimac.h
 *
 *  Created on: 2013-2-22
 *      Author: yanglin
 */

#ifndef STACTRLWIFIMAC_H_
#define STACTRLWIFIMAC_H_

#include "ns3/regular-ctrl-wifi-mac.h"

#include "ns3/event-id.h"
#include "ns3/packet.h"
#include "ns3/traced-callback.h"

#include "ns3/supported-rates.h"
#include "ns3/amsdu-subframe-header.h"

namespace ns3
{

class StaCtrlWifiMac: public ns3::RegularCtrlWifiMac
{
public:
  static TypeId GetTypeId (void);

  StaCtrlWifiMac ();
  virtual ~StaCtrlWifiMac ();

  /**
   * \param packet the packet to send.
   * \param to the address to which the packet should be sent.
   *
   * The packet should be enqueued in a tx queue, and should be
   * dequeued as soon as the channel access function determines that
   * access is granted to this MAC.
   */
  virtual void Enqueue (Ptr<const Packet> packet, Mac48Address to);

  /**
   * \param missed the number of beacons which must be missed
   * before a new association sequence is started.
   */
  void SetMaxMissedBeacons (uint32_t missed);
  /**
   * \param timeout
   *
   * If no probe response is received within the specified
   * timeout, the station sends a new probe request.
   */
  void SetProbeRequestTimeout (Time timeout);
  /**
   * \param timeout
   *
   * If no association response is received within the specified
   * timeout, the station sends a new association request.
   */
  void SetAssocRequestTimeout (Time timeout);

  /**
   * Start an active association sequence immediately.
   */
  void StartActiveAssociation (void);


private:
  enum MacState
  {
    ASSOCIATED,
    WAIT_PROBE_RESP,
    WAIT_ASSOC_RESP,
    BEACON_MISSED,
    REFUSED
  };

  void SetActiveProbing (bool enable);
  bool GetActiveProbing (void) const;
  virtual void Receive (Ptr<Packet> packet, const WifiMacHeader *hdr);
  void SendProbeRequest (void);
  void SendAssociationRequest (void);
  void TryToEnsureAssociated (void);
  void AssocRequestTimeout (void);
  void ProbeRequestTimeout (void);
  bool IsAssociated (void) const;
  bool IsWaitAssocResp (void) const;
  void MissedBeacons (void);
  void RestartBeaconWatchdog (Time delay);
  SupportedRates GetSupportedRates (void) const;
  void SetState (enum MacState value);

  enum MacState m_state;
  Time m_probeRequestTimeout;
  Time m_assocRequestTimeout;
  EventId m_probeRequestEvent;
  EventId m_assocRequestEvent;
  EventId m_beaconWatchdog;
  Time m_beaconWatchdogEnd;
  uint32_t m_maxMissedBeacons;

  TracedCallback<Mac48Address> m_assocLogger;
  TracedCallback<Mac48Address> m_deAssocLogger;
};

} /* namespace ns3 */
#endif /* STACTRLWIFIMAC_H_ */
