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
 * Author: Mohamed Amine Ismail <amine.ismail@sophia.inria.fr>
 */
#include "lin-udp-client-server-helper.h"
#include "ns3/lin-udp-server.h"
#include "ns3/lin-udp-client.h"
#include "ns3/udp-trace-client.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"

namespace ns3 {

LinUdpServerHelper::LinUdpServerHelper ()
{
}

LinUdpServerHelper::LinUdpServerHelper (uint16_t port)
{
  m_factory.SetTypeId (LinUdpServer::GetTypeId ());
  SetAttribute ("Port", UintegerValue (port));
}

void
LinUdpServerHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
LinUdpServerHelper::Install (NodeContainer c, Ptr<NetDevice> netDevice)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;

      m_server = m_factory.Create<LinUdpServer> ();
      m_server->SetBoundNetDevice(netDevice);

      node->AddApplication (m_server);
      apps.Add (m_server);

    }
  return apps;
}

Ptr<LinUdpServer>
LinUdpServerHelper::GetServer (void)
{
  return m_server;
}

LinUdpClientHelper::LinUdpClientHelper ()
{
}

LinUdpClientHelper::LinUdpClientHelper (Address address, uint16_t port)
{
  m_factory.SetTypeId (LinUdpClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (address));
  SetAttribute ("RemotePort", UintegerValue (port));
}

LinUdpClientHelper::LinUdpClientHelper (Ipv4Address address, uint16_t port)
{
  m_factory.SetTypeId (LinUdpClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (Address(address)));
  SetAttribute ("RemotePort", UintegerValue (port));
}

LinUdpClientHelper::LinUdpClientHelper (Ipv6Address address, uint16_t port)
{
  m_factory.SetTypeId (LinUdpClient::GetTypeId ());
  SetAttribute ("RemoteAddress", AddressValue (Address(address)));
  SetAttribute ("RemotePort", UintegerValue (port));
}

void
LinUdpClientHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
LinUdpClientHelper::Install (NodeContainer c, Ptr<NetDevice> netDevice)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      Ptr<Node> node = *i;
      Ptr<LinUdpClient> client = m_factory.Create<LinUdpClient> ();
      client->SetBoundNetDevice(netDevice);
      node->AddApplication (client);
      apps.Add (client);
    }
  return apps;
}

//LinUdpTraceClientHelper::LinUdpTraceClientHelper ()
//{
//}
//
//LinUdpTraceClientHelper::LinUdpTraceClientHelper (Address address, uint16_t port, std::string filename)
//{
//  m_factory.SetTypeId (UdpTraceClient::GetTypeId ());
//  SetAttribute ("RemoteAddress", AddressValue (address));
//  SetAttribute ("RemotePort", UintegerValue (port));
//  SetAttribute ("TraceFilename", StringValue (filename));
//}
//
//LinUdpTraceClientHelper::LinUdpTraceClientHelper (Ipv4Address address, uint16_t port, std::string filename)
//{
//  m_factory.SetTypeId (UdpTraceClient::GetTypeId ());
//  SetAttribute ("RemoteAddress", AddressValue (Address (address)));
//  SetAttribute ("RemotePort", UintegerValue (port));
//  SetAttribute ("TraceFilename", StringValue (filename));
//}
//
//LinUdpTraceClientHelper::LinUdpTraceClientHelper (Ipv6Address address, uint16_t port, std::string filename)
//{
//  m_factory.SetTypeId (UdpTraceClient::GetTypeId ());
//  SetAttribute ("RemoteAddress", AddressValue (Address (address)));
//  SetAttribute ("RemotePort", UintegerValue (port));
//  SetAttribute ("TraceFilename", StringValue (filename));
//}
//
//void
//LinUdpTraceClientHelper::SetAttribute (std::string name, const AttributeValue &value)
//{
//  m_factory.Set (name, value);
//}
//
//ApplicationContainer
//LinUdpTraceClientHelper::Install (NodeContainer c, Ptr<NetDevice> netDevice)
//{
//  ApplicationContainer apps;
//  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
//    {
//      Ptr<Node> node = *i;
//      Ptr<UdpTraceClient> client = m_factory.Create<UdpTraceClient> ();
//      node->AddApplication (client);
//      apps.Add (client);
//    }
//  return apps;
//}

} // namespace ns3
