/*
 * align.{cc,hh} -- element aligns passing packets
 * Eddie Kohler
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
#include "align.hh"
#include "glue.hh"
#include "confparse.hh"
#include "error.hh"

Align::Align()
  : Element(1, 1)
{
}

int
Align::configure(const Vector<String> &conf, ErrorHandler *errh)
{
  unsigned count;
  if (cp_va_parse(conf, this, errh,
		  cpUnsigned, "alignment modulus", &count,
		  cpUnsigned, "alignment offset", &_offset,
		  0) < 0)
    return -1;
  if (count != 2 && count != 4 && count != 8)
    return errh->error("align modulus must be 2, 4, or 8");
  if (_offset >= (int)count)
    return errh->error("align offset must be smaller than modulus");
  _mask = count - 1;
  return 0;
}

Packet *
Align::smaction(Packet *p)
{
  int delta =
    _offset - (reinterpret_cast<unsigned long>(p->data()) & _mask);
  if (delta == 0)
    return p;
  else if (delta < 0)
    delta += _mask + 1;
  if (!p->shared() && p->tailroom() >= (unsigned)delta) {
    WritablePacket *q = reinterpret_cast<WritablePacket *>(p);
    memmove(q->data() + delta, q->data(), q->length());
    q->pull(delta);
    q->put(delta);
    return q;
  } else {
    WritablePacket *q = Packet::make(p->headroom() + delta, p->data(), p->length(), p->tailroom());
    q->copy_annotations(p);
    p->kill();
    return q;
  }
}

void
Align::push(int, Packet *p)
{
  output(0).push(smaction(p));
}

Packet *
Align::pull(int)
{
  Packet *p = input(0).pull();
  return (p ? smaction(p) : 0);
}

EXPORT_ELEMENT(Align)
