/*
 * netaddresstranslator{cc,hh} -- element that translate an IPv6 packet to an IPv4 packet or translate an IPv4 jpacket to an IPv6 packet
 * 
 * Peilei Fan
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Further elaboration of this license, including a DISCLAIMER OF ANY
 * WARRANTY, EXPRESS OR IMPLIED, is provided in the LICENSE file, which is
 * also accessible at http://www.pdos.lcs.mit.edu/click/license.html
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "netaddresstranslator.hh"
#include "confparse.hh"
#include "error.hh"
#include "click_ip.h"
#include "click_ip6.h"
#include "click_icmp.h"
#include "click_icmp6.h"
#include "ip6address.hh"

NetAddressTranslator::NetAddressTranslator()
{
  add_input(); /*IPv6 packets */
  add_input();  /*IPv4 packets */
  add_output(); /* IPv4 packets */
  add_output(); /* IPv6 packets */
}


NetAddressTranslator::~NetAddressTranslator()
{
}

void
NetAddressTranslator::add_map(IPAddress ipa4, IP6Address ipa6, bool bound)
{ 
  struct Entry64 e;
  e._ip6=ipa6;
  e._ip4=ipa4;
  e._bound = bound;
  _v.push_back(e);
}


int
NetAddressTranslator::configure(const Vector<String> &conf, ErrorHandler *errh)
{
  _v.clear();

  int before= errh->nerrors();
  for (int i = 0; i<conf.size(); i++) 
    {
      IP6Address ipa6;
      IPAddress ipa4;

      Vector<String> words;
      cp_spacevec(conf[i], words);

      for (int j = 0; j < words.size(); j++)
	if (cp_ip_address(words[j], (unsigned char *)&ipa4)) {
	  if (j < words.size() - 1
	      && cp_ip6_address(words[j+1], (unsigned char *)&ipa6)) {
	    add_map(ipa4, ipa6, 1);
	    j++;
	  } else
	    add_map(ipa4, IP6Address("::0"), 0);
	}
    }
  return (before ==errh->nerrors() ? 0: -1);
}


//check if there's already binding for the ipv6 address, if not, then assign the first unbinded ipv4 address to it.  If there's no ipv4 address available, return false.  

bool
NetAddressTranslator::lookup(IP6Address ipa6, IPAddress &ipa4)
{ 
  int i = -1;

  for (i=0; i<_v.size(); i++) {
    if ((ipa6 == _v[i]._ip6) && (_v[i]._bound)) {
      ipa4 = _v[i]._ip4;
      return (true);
    }
    else if ((ipa6 == _v[i]._ip6 ) && (_v[i]._bound==0)) {
      _v[i]._bound = 1;
      ipa4 = _v[i]._ip4;
      return (true);
    }
    else if (_v[i]._bound == 0 )
      {
	_v[i]._bound =1;
	ipa4=_v[i]._ip4;
	_v[i]._ip6 = ipa6;
	return (true);
      }
  }

  return (false);
}
	
bool
NetAddressTranslator::reverse_lookup(IPAddress ipa4, IP6Address &ipa6)
{
  int i=-1;

  for (i=0; i<_v.size(); i++) {
    if ((ipa4 ==_v[i]._ip4) && (_v[i]._bound)) {
      ipa6=_v[i]._ip6;
      return (true);
    }
  }
  return (false);
}

//make the ipv6->ipv4 translation of the packet according to SIIT (RFC 2765)
Packet *
NetAddressTranslator::make_translate64(IPAddress src,
			 IPAddress dst,
			 unsigned short ip6_plen, 
			 unsigned char ip6_hlim, 
			 unsigned char ip6_nxt,
			 unsigned char *a)
{
  click_ip *ip;
  unsigned char * b;
  WritablePacket *q = Packet::make(sizeof(*ip) + ntohs(ip6_plen));
  
  if (q==0) {
    click_chatter("can not make packet!");
    assert(0);
  }

  memset(q->data(), '\0', q->length());
  ip = (click_ip *)q->data();
  b = (unsigned char * ) (ip+1);
  
  //set ipv4 header
  ip->ip_v = 4;
  ip->ip_hl =5; 
  ip->ip_tos =0;
  ip->ip_len = htons(sizeof(*ip) + ntohs(ip6_plen));
  
  ip->ip_id = htons(0);

  //set Don't Fragment flag to true, all other flags to false, 
  //set fragement offset: 0
  ip->ip_off = htons(IP_DF);
  //we do not change the ttl since the packet has to go through v4 routing table
  ip->ip_ttl = ip6_hlim;

  if (ip6_nxt ==58) {
    ip->ip_p=1; 
  } else {
    ip->ip_p = ip6_nxt;
  }
 
  //set the src and dst address
  ip->ip_src = src.in_addr();
  ip->ip_dst = dst.in_addr();
 
  //copy the actual payload of packet 
  memcpy(b, a, ntohs(ip6_plen));

  //set the ip header checksum
  ip->ip_sum=0;
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(click_ip));
  return q;	
		
}

//make the ipv4->ipv6 translation of the packet according to SIIT (RFC 2765)
Packet * 
NetAddressTranslator::make_translate46(IP6Address src, 
				       IP6Address dst,
				       unsigned short ip_len,
				       unsigned char ip_p, 
				       unsigned char ip_ttl,
				       unsigned char *a)
{
  //click_chatter("make_translate46 -1");
  click_ip6 *ip6;
  unsigned char *b;
  WritablePacket *q = Packet::make(sizeof(*ip6)+ntohs(ip_len)-sizeof(click_ip));

  if (q==0) {
    click_chatter("can not make packet!");
    assert(0);
  }

  memset(q->data(), '\0', q->length());
  ip6=(click_ip6 *)q->data();
  b=(unsigned char *)(ip6+1);

  //set ipv6 header
  ip6->ip6_v=6;
  ip6->ip6_pri=0;
  for (int i=0; i<3; i++)
    ip6->ip6_flow[i]=0;
  ip6->ip6_plen = htons(ntohs(ip_len)-sizeof(click_ip));
  if (ip_p == 1)
    ip6->ip6_nxt=58;
  else
    ip6->ip6_nxt = ip_p;
  ip6->ip6_hlim = ip_ttl;
  ip6->ip6_src = src;
  ip6->ip6_dst = dst;
  memcpy(b, a, ntohs(ip6->ip6_plen));
  return q;
  
}


Packet *
NetAddressTranslator::make_icmp_translate64(unsigned char *a,
			      unsigned char payload_length)
{
  
  payload_length = payload_length - sizeof(click_ip6) + sizeof(click_ip);
  click_ip6 *ip6=0;
  unsigned char *ip=0;
  unsigned char icmp6_type= a[0];
  unsigned char icmp6_code= a[1];
  unsigned int icmp6_pointer = (unsigned int)a[4];
  unsigned char icmp_length =sizeof(icmp_generic)+sizeof(click_ip) + 8;
  WritablePacket *q2 = 0;

  // set type, code, length , and checksum of ICMP header
  switch (icmp6_type) {
    //if ( (icmp6_type == ICMP6_ECHO_REQUEST ) || (icmp6_type == ICMP6_ECHO_REPLY )) 
  case ICMP6_ECHO_REQUEST: ;
  case ICMP6_ECHO_REPLY : {
      icmp6_echo *icmp6 = (icmp6_echo *)a;
      ip6 = (click_ip6 *)(icmp6+1);
      q2 = Packet::make(icmp_length);
      memset(q2->data(), '\0', q2->length());
      icmp_echo *icmp = (icmp_echo *)q2->data();
      ip = (unsigned char *)(icmp+1);
      
      if (icmp6_type == ICMP6_ECHO_REQUEST ) { // icmp6_type ==128 
	icmp->icmp_type = ICMP_ECHO;                 // icmp_type =8  
      }
      else if (icmp6_type == ICMP6_ECHO_REPLY ) { // icmp6_type ==129 
	icmp->icmp_type = ICMP_ECHO_REPLY;              // icmp_type = 0   
      }
      icmp->icmp_cksum = in_cksum((unsigned char *)icmp, sizeof(icmp_echo));
    }
  break;
 
  case  ICMP6_DST_UNREACHABLE : {
   // icmp6_type ==11 
      icmp6_dst_unreach *icmp6 = (icmp6_dst_unreach *)a;
      ip6 = (click_ip6 *)(icmp6+1);
      q2 = Packet::make(icmp_length);
      memset(q2->data(), '\0', q2->length());
      icmp_unreach *icmp = (icmp_unreach *)q2->data();
      ip = (unsigned char *)(icmp+1);
      icmp->icmp_type = ICMP_DST_UNREACHABLE;             // icmp_type =3    
   
      switch (icmp6_code) {
      case 0: icmp->icmp_code =0;  break;
      case 1: icmp->icmp_code =10; break;
      case 2: icmp->icmp_code =5;  break;
      case 3: icmp->icmp_code =1;  break;
      case 4: icmp->icmp_code =3;  break;
      default: ; break;
      }

      icmp->icmp_cksum = in_cksum((unsigned char *)icmp, sizeof(icmp_unreach));
    
    }
  break;
  
  case ICMP6_PKT_TOOBIG : {
      icmp6_pkt_toobig *icmp6 = (icmp6_pkt_toobig*)a;
      ip6 = (click_ip6 *)(icmp6+1);
      q2 = Packet::make(icmp_length);
      memset(q2->data(), '\0', q2->length());
      icmp_unreach *icmp = (icmp_unreach *)q2->data();
      ip = (unsigned char *)(icmp+1);
      icmp->icmp_type = ICMP_DST_UNREACHABLE; //icmp_type = 3
      icmp->icmp_code = 4;
      icmp->icmp_cksum = in_cksum((unsigned char *)icmp, sizeof(icmp_unreach));
    }
  break;
  
  case ICMP6_TYPE_TIME_EXCEEDED : { 
      icmp6_time_exceeded *icmp6 = (icmp6_time_exceeded *)a;
      ip6 = (click_ip6 *)(icmp6+1);
      q2 = Packet::make(icmp_length);
      memset(q2->data(), '\0', q2->length());
      icmp_exceeded *icmp = (icmp_exceeded *)q2->data();
      ip = (unsigned char *)(icmp+1);
      icmp->icmp_type =ICMP_TYPE_TIME_EXCEEDED;            //icmp_type = 11 
      icmp->icmp_code = icmp6_code;
      icmp->icmp_cksum = in_cksum((unsigned char *)icmp, sizeof(icmp_exceeded));
    }
  break;
  
  case ICMP6_PARAMETER_PROBLEM : { // icmp6_type == 4
      icmp6_param *icmp6 = (icmp6_param *)a;
      ip6=(click_ip6 *)(icmp6+1);
      if (icmp6_code ==2 || icmp6_code ==0) 
	{
	  q2 = Packet::make(icmp_length);
	  memset(q2->data(), '\0', q2->length());
	  icmp_param *icmp = (icmp_param *)q2->data();
	  ip = (unsigned char *)(icmp +1);
	  icmp->icmp_type = ICMP_PARAMETER_PROBLEM;         // icmp_type = 12 
	  icmp->icmp_code = 0;
	  if (icmp6_code ==2) {
	    icmp->pointer = -1;
	  }
	  else if(icmp6_code ==0)
	    {
	      switch (icmp6_pointer) {
	      case 0 : icmp->pointer = 0;  break;
	      case 4 : icmp->pointer = 2;  break;
	      case 7 : icmp->pointer = 8;  break;
	      case 6 : icmp->pointer = 9;  break;
	      case 8 : icmp->pointer = 12; break;
	      case 24: icmp->pointer = -1; break;
	      default: ; break; 
	      }
	      
	    }
	  icmp->icmp_cksum = in_cksum((unsigned char *)icmp, sizeof(icmp_param));
	}

      else if (icmp6_code ==1) 
	{
	  q2 = Packet::make(icmp_length);
	  memset(q2->data(), '\0', q2->length());
	  icmp_unreach *icmp = (icmp_unreach *)q2->data();
      
	  ip = (unsigned char *)(icmp +1);
	  icmp->icmp_type = ICMP_DST_UNREACHABLE;           // icmp_type = 3 
	  icmp->icmp_code = 2;
	  icmp->icmp_cksum = in_cksum((unsigned char *)icmp, sizeof(icmp_unreach));
	}
  }
  break;
  
  default: ; break;
  }

    //translate the embeded IP6 packet to (IPheader + 8 bytes)
  Packet *q3=0;
  unsigned char * start_of_q3_content = (unsigned char *)(ip6 +1);
  unsigned char ip4_dst[4];
  unsigned char ip4_src[4];
  IP6Address ip6_dst;
  IP6Address ip6_src;
  IPAddress ip4_dsta;
  IPAddress ip4_srca;
  ip6_dst = IP6Address(ip6->ip6_dst);
  ip6_src = IP6Address(ip6->ip6_src);
  if ((ip6_dst.get_ip4address(ip4_dst) || lookup(ip6_dst, ip4_dsta)) &&
      (ip6_src.get_ip4address(ip4_src) || lookup(ip6_src, ip4_srca)))   
    {
      if (ip6_dst.get_ip4address(ip4_dst))
	ip4_dsta = IPAddress(ip4_dst);
      if (ip6_src.get_ip4address(ip4_src))
	ip4_srca = IPAddress(ip4_src);
      q3 = make_translate64(ip4_srca, ip4_dsta, ip6->ip6_plen, ip6->ip6_hlim, ip6->ip6_nxt, start_of_q3_content);
       
    }
  else {
    click_chatter("no addresses available for translating");
  }
  
  memcpy(ip, q3->data(), 28);
  q3->kill();
  return q2;
}



Packet *
NetAddressTranslator::make_icmp_translate46(IP6Address ip6_src,
					    IP6Address ip6_dst,
					    unsigned char *a,
					    unsigned char payload_length)
{
  click_ip *ip=0;
  unsigned char *ip6=0;
  unsigned char icmp_type = a[0];
  unsigned char icmp_code = a[1];
  unsigned char icmp_pointer = a[4];

  unsigned char icmp6_code = 0; 
  unsigned char icmp6_length;
  
  WritablePacket *q2 = 0;

  switch (icmp_type)  {
  case (ICMP_ECHO): ; // icmp_type ==8
  case (ICMP_ECHO_REPLY):  {//icmp_type ==0 
    icmp_echo *icmp = (icmp_echo *)a;
    ip = (click_ip *)(icmp+1);
    icmp6_length = payload_length-sizeof(icmp_echo)+sizeof(icmp6_echo)-sizeof(click_ip)+sizeof(click_ip6);
    q2=Packet::make(icmp6_length);
    memset(q2->data(), '\0', q2->length());
    icmp6_echo *icmp6 = (icmp6_echo *)q2->data();
    ip6=(unsigned char *)(icmp6+1);
      
    if (icmp_type == ICMP_ECHO ) { // icmp_type ==8
      icmp6->icmp6_type = ICMP6_ECHO_REQUEST;  // icmp6_type =128  
    }
    else if (icmp_type == ICMP_ECHO_REPLY ) { // icmp_type ==0
      icmp6->icmp6_type = ICMP6_ECHO_REPLY;              // icmp6_type = 129   
    }
    icmp6->icmp6_cksum = htons(in6_fast_cksum(&ip6_src.addr(), &ip6_dst.addr(), icmp6_length, 0x3a, 0, (unsigned char *)icmp6, sizeof(icmp6_generic)));
  }
  break; 

  case (ICMP_DST_UNREACHABLE):  { //icmp_type ==3
    icmp_unreach *icmp = (icmp_unreach *)a;
    ip = (click_ip *)(icmp+1);
 
    if (icmp_code == 2 || icmp_code ==4) {
      switch (icmp_code) {
      case 2:   {
      icmp6_length = payload_length-sizeof(icmp_unreach)+sizeof(icmp6_param)-sizeof(click_ip)+sizeof(click_ip6);
      q2=Packet::make(icmp6_length);
      memset(q2->data(), '\0', q2->length());
      icmp6_param *icmp6 = (icmp6_param *)q2->data();
      ip6=(unsigned char *)(icmp6+1);
      icmp6->icmp6_type = ICMP6_PARAMETER_PROBLEM; //icmp6_type = 4
      icmp6->icmp6_code = 1;
      icmp6->pointer = 6;
      }
      break;
	    
      case 4: {
      icmp6_length = payload_length-sizeof(icmp_unreach)+sizeof(icmp6_pkt_toobig)-sizeof(click_ip)+sizeof(click_ip6);
      q2=Packet::make(icmp6_length);
      memset(q2->data(), '\0', q2->length());
      icmp6_pkt_toobig *icmp6 = (icmp6_pkt_toobig *)q2->data();
      ip6=(unsigned char *)(icmp6+1);
      icmp6->icmp6_type = ICMP6_PKT_TOOBIG; //icmp6_type = 2
      icmp6->icmp6_code = 0;

      //adjust the mtu field for the difference between the ipv4 and ipv6 header size
      }
      break;
      default: ; 
      }
    }
    else {
    
      switch (icmp_code) {
      case 0 : ;
      case 1 : ;
      case 6 : ; 
      case 7 : ;
      case 8 : ;
      case 11: ;
      case 12: {
	icmp6_code = 0;
      }
      break;
      case 3 : {
	icmp6_code = 4; 
      }
      break;
      case 5 : {
	icmp6_code = 2;
      }
      break;
      case 9 : ;
      case 10: {
	icmp6_code = 1; 
      }
      break;
      default:  {
	icmp6_length = payload_length-sizeof(icmp_unreach)+sizeof(icmp6_dst_unreach)-sizeof(click_ip)+sizeof(click_ip6);
	q2=Packet::make(icmp6_length);
	memset(q2->data(), '\0', q2->length());
	icmp6_dst_unreach *icmp6 = (icmp6_dst_unreach *)q2->data();
	ip6=(unsigned char *)(icmp6+1);
	icmp6->icmp6_type = ICMP6_DST_UNREACHABLE;
	icmp6->icmp6_code = icmp6_code;
	icmp6->icmp6_cksum = htons(in6_fast_cksum(&ip6_src.addr(), &ip6_dst.addr(), icmp6_length, 0x3a, 0, (unsigned char *)icmp6, sizeof(icmp6_generic)));
      }
      break;
      }
    }
  }
  break; 
     
  case (ICMP_TYPE_TIME_EXCEEDED) : { //icmp ==11
    icmp_exceeded *icmp = (icmp_exceeded *)a;
    ip = (click_ip *)(icmp+1);
    icmp6_length = payload_length-sizeof(icmp_exceeded)-sizeof(click_ip)+sizeof(click_ip6)+sizeof(icmp6_time_exceeded);
    q2=Packet::make(icmp6_length);
    memset(q2->data(), '\0', q2->length());
    icmp6_echo *icmp6 = (icmp6_echo *)q2->data();
    ip6=(unsigned char *)(icmp6+1);
    icmp6->icmp6_type=ICMP6_TYPE_TIME_EXCEEDED;
    icmp6->icmp6_code = icmp_code;
    icmp6->icmp6_cksum = htons(in6_fast_cksum(&ip6_src.addr(), &ip6_dst.addr(), icmp6_length, 0x3a, 0, (unsigned char *)icmp6, sizeof(icmp6_generic)));
  }
  break;

  case (ICMP_PARAMETER_PROBLEM): { //icmp==12
    icmp_param *icmp = (icmp_param *)a;
    ip = (click_ip *)(icmp+1);
    icmp6_length = payload_length-sizeof(icmp_param)+sizeof(icmp6_param)-sizeof(click_ip)+sizeof(click_ip6);
    q2=Packet::make(icmp6_length);
    memset(q2->data(), '\0', q2->length());
    icmp6_param *icmp6 = (icmp6_param *)q2->data();
    ip6=(unsigned char *)(icmp6+1);
    icmp6->icmp6_type=ICMP6_PARAMETER_PROBLEM;
	  
    switch (icmp_pointer) {
    case 0  : icmp6->pointer = 0;  break;
    case 2  : icmp6->pointer = 4;  break;
    case 8  : icmp6->pointer = 7;  break;
    case 9  : icmp6->pointer = 6;  break;
    case 12 : icmp6->pointer = 8;  break;
    case 16 : icmp6->pointer = 24; break;
    default : icmp6->pointer = -1; break;
    }
    icmp6->icmp6_cksum = htons(in6_fast_cksum(&ip6_src.addr(), &ip6_dst.addr(), icmp6_length, 0x3a, 0, (unsigned char *)icmp6, sizeof(icmp6_generic)));
  }
  break;

  default: ;
  }

  //translated the embeded ip packet to ip6 packet
  Packet *q3=0;
  unsigned char * start_of_q3_content = (unsigned char *) (ip+1);
  IP6Address ip6a_dst;
  IP6Address ip6a_src;
 

  if (!reverse_lookup(IPAddress(ip->ip_dst), ip6a_dst)) {
    ip6a_dst = IP6Address(IPAddress(ip->ip_dst));	
  }
  if (!reverse_lookup(IPAddress(ip->ip_src), ip6a_src)) { 
    ip6a_src = IP6Address(IPAddress(ip->ip_src));
  }
  q3 = make_translate46(ip6a_src, ip6a_dst, ip->ip_len, ip->ip_ttl, ip->ip_p, start_of_q3_content);

  memcpy(ip6, q3->data(), 48); 
  
  q3->kill();
  return q2;
   
}


void
NetAddressTranslator::push(int port, Packet *p)
{
  if (port == 0)
    handle_ip6(p);
  else 
    handle_ip4(p);
}


void 
NetAddressTranslator::handle_ip6(Packet *p)
{
  click_ip6 *ip6 = (click_ip6 *) p->data();
  unsigned char * start_of_p= (unsigned char *)(ip6+1);
  IPAddress ipa_src;
  IPAddress ipa_dst; 

 
  Packet *q = 0;
  unsigned char ip4_dst[4];
  IP6Address ip6_dst;
  ip6_dst = IP6Address(ip6->ip6_dst);
  
  // get the corresponding ipv4 src and dst addresses
  if (ip6_dst.get_ip4address(ip4_dst)
      && lookup(IP6Address(ip6->ip6_src), ipa_src))
    {
      ipa_dst = IPAddress(ip4_dst);
      //translate protocol according to SIIT
      q = make_translate64(ipa_src, ipa_dst, ip6->ip6_plen, ip6->ip6_hlim, ip6->ip6_nxt, start_of_p);
      //see if it has the icmp6 packet, if it does, 
      //we need to translate the icmp6 and its embedded ip6 header 
      //the final packet will be "ip_header+icmp_header+ip_header+8 bytes of ip packet data"
      if (ip6->ip6_nxt == 0x3a) 
	{
	  click_chatter("This is also a ICMP6 Packet!");
	  click_ip * ip4= (click_ip *)q->data();
	  unsigned char * icmp6 = (unsigned char *)(ip4+1);
	  Packet *q2 = 0;
	  q2 = make_icmp_translate64(icmp6, (q->length()-sizeof(click_ip)));
	  WritablePacket *q3=Packet::make(sizeof(click_ip)+q2->length());
	  memset(q3->data(), '\0', q3->length());
	  click_ip *start_of_q3 = (click_ip *)q3->data();
	  memcpy(start_of_q3, q->data(), sizeof(click_ip));
	  unsigned char *start_of_icmp = (unsigned char *)(start_of_q3+1);
	  memcpy(start_of_icmp, q2->data(), q2->length()); 
	  click_ip * ip=(click_ip *)q3->data();
	  ip->ip_len = htons(q3->length());
	  ip->ip_sum=0;
	  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(click_ip));

	  p->kill();
	  q->kill();
	  q2->kill();
	  output(0).push(q3);
	
	}
      else 
	{
	  p->kill();
	  output(0).push(q);
	}
    }
  else {
    p->kill();
  }
}

void 
NetAddressTranslator::handle_ip4(Packet *p)
{
  click_ip *ip = (click_ip *)p->data();
  unsigned char *start_of_p = (unsigned char *)(ip+1);
  IP6Address ip6a_src = IP6Address(IPAddress(ip->ip_src));
  IP6Address ip6a_dst;
  Packet *q = 0;

  if (reverse_lookup(IPAddress(ip->ip_dst), ip6a_dst))
    {
      q=make_translate46(ip6a_src, ip6a_dst, ip->ip_len, ip->ip_p, ip->ip_ttl, start_of_p);
      
      if (ip->ip_p == 1)
	{
	  click_ip6 * ip6 = (click_ip6 *)q->data();
	  unsigned char * icmp = (unsigned char *)(ip6+1);
	  Packet *q2 = 0;
	  q2 = make_icmp_translate46(ip6a_src, ip6a_dst, icmp, (q->length() - sizeof(click_ip6)));
	  WritablePacket *q3 = Packet::make(sizeof(click_ip6)+q2->length());
	  memset(q3->data(), '\0', q3->length());
	  click_ip6 *start_of_q3 = (click_ip6 *)q3->data();
	  memcpy(start_of_q3, q->data(), sizeof(click_ip6));
	  unsigned char *start_of_icmp6 = (unsigned char *)(start_of_q3+1);
	  memcpy(start_of_icmp6, q2->data(), q2->length()); 
	  click_ip6 * ip62=(click_ip6 *)q3->data();
	  ip62->ip6_plen = htons(q3->length()-sizeof(click_ip6));
	  

	  p->kill();
	  q->kill();
	  q2->kill();
	  output(1).push(q3);
	
	}
      else {
	p->kill();
	output(1).push(q);
      }
    }
  else {
    p->kill();
  }
}

EXPORT_ELEMENT(NetAddressTranslator)

// generate Vector template instance
#include "vector.cc"
template class Vector<NetAddressTranslator::Entry64>;
