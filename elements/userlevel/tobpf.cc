/*
 * tobpf.{cc,hh} -- element writes packets to network via pcap library
 * John Jannotti
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
#include "tobpf.hh"
#include "confparse.hh"
#include "error.hh"

ToBPF::ToBPF()
{
}

ToBPF *
ToBPF::clone() const
{
  return new ToBPF;
}

int
ToBPF::configure(const Vector<String> &conf, ErrorHandler *errh)
{
  String ifname;
  if (cp_va_parse(conf, this, errh,
		  cpString, "interface name", &ifname,
		  0) < 0)
    return -1;
  return errh->error("ToBPF has been deprecated; use ToDevice instead");
}

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(ToBPF)
