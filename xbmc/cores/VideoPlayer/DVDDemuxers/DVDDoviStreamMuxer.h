/*
 *  (c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <stdint.h>

#include "DVDStreamMuxer.h"

class DynamicMemory;

class DVDDoviStreamMuxer : public DVDStreamMuxer
{
private:
	DynamicMemory	*m_dynm;
	bool			 m_isHvccAtom;

public:
	DVDDoviStreamMuxer(CDemuxStreamVideo *main, CDemuxStreamVideo *extension);
	virtual ~DVDDoviStreamMuxer();

protected:
	virtual DemuxPacket *mergePackets(DemuxPacket *srcPkt, DemuxPacket *extPkt);
	virtual DemuxPacket *convertPacket(DemuxPacket *srcPkt) const;
	virtual DemuxPacket *convertHvccPacket(DemuxPacket *srcPkt) const;
	virtual uint8_t *findStartCode(uint8_t *startp, uint8_t *endp) const;

private:
	DemuxPacket *dynMemToDemuxPacket(DemuxPacket *srcPkt) const;
};
