/*
 * desp.{cc,hh} -- element implements IPsec unencapsulation (RFC 2406)
 * Alex Snoeren
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
#ifndef HAVE_IPSEC
# error "Must #define HAVE_IPSEC in config.h"
#endif
#include "esp.hh"
#include "desp.hh"
#include "ipaddress.hh"
#include "confparse.hh"
#include "error.hh"
#include "glue.hh"

DeEsp::DeEsp()
  : Element(1, 1)
{
}

DeEsp::~DeEsp()
{
}

DeEsp *
DeEsp::clone() const
{
  return new DeEsp();
}

int
DeEsp::configure(const Vector<String> &conf, ErrorHandler *errh)
{
  if (cp_va_parse(conf, this, errh,
		  0) < 0)
    return -1;
  return 0;
}


Packet *
DeEsp::simple_action(Packet *p)
{

  int i, blks;
  const unsigned char * blk;

  // Rip off ESP header
  p->pull(sizeof(esp_new));

  // Verify padding
  blks = p->length();
  click_chatter("got %d left", blks);
  blk = p->data();
  if((blk[blks - 2] != blk[blks - 3]) && (blk[blks -2] != 0)) {
    click_chatter("Invalid padding length");
    p->kill();
    return(0);
  }
  blks = blk[blks - 2];
  blk = p->data() + p->length() - (blks + 2);
  for(i = 0; (i < blks) && (blk[i] == ++i););    
  if(i<blks) {
    click_chatter("Corrupt padding");
    p->kill();
    return(0);
  }

  // Chop off padding
  return Packet::make(p->data(), p->length() - (blks + 2));
}

ELEMENT_REQUIRES(false)
EXPORT_ELEMENT(DeEsp)
