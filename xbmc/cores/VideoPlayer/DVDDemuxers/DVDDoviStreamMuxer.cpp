/*
 *  (c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDDoviStreamMuxer.h"

#include "DVDDemux.h"
#include "DVDDemuxUtils.h"

#include "utils/DynamicMemory.h"
#include "utils/log.h"

static const unsigned rsvdSize = 0x1000;
static const unsigned initialDynMemSize = 100 * 1024;
static const uint8_t unspec63[] = { 0x7e, 0x01 };

DVDDoviStreamMuxer::DVDDoviStreamMuxer(CDemuxStreamVideo *main, CDemuxStreamVideo *extension)
	: DVDStreamMuxer(main, extension), m_dynm(nullptr)
{
	m_dynm = new DynamicMemory(initialDynMemSize);
}

DVDDoviStreamMuxer::~DVDDoviStreamMuxer()
{
	if (m_dynm) {
		delete m_dynm, m_dynm = nullptr;
	}
}

DemuxPacket *DVDDoviStreamMuxer::mergePackets(DemuxPacket *srcPkt, DemuxPacket *extPkt)
{
	DemuxPacket *cvtPkt = convertPacket(extPkt);

	return DVDStreamMuxer::mergePackets(srcPkt, cvtPkt);
}

DemuxPacket *DVDDoviStreamMuxer::convertPacket(DemuxPacket *srcPkt) const
{
	uint8_t *oldp = srcPkt->pData;
	uint8_t *endp = oldp + srcPkt->iSize;
	uint8_t *newp = nullptr;

	m_dynm->allocate(srcPkt->iSize + rsvdSize);
	m_dynm->clear();

	while (oldp != endp) {
		newp = findStartCode(oldp, endp);

		m_dynm->append(oldp, newp - oldp);

		if (newp != endp && *newp != 0x7c) {
			m_dynm->append((void*)unspec63, sizeof(unspec63));
		}

		oldp = newp;
	}

	DemuxPacket *newpkt = CDVDDemuxUtils::AllocateDemuxPacket(m_dynm->size());
	newpkt->iSize = m_dynm->size();
	newpkt->pts = srcPkt->pts;
	newpkt->dts = srcPkt->dts;
	newpkt->duration = srcPkt->duration;
	newpkt->iGroupId = srcPkt->iGroupId;
	newpkt->iStreamId = srcPkt->iStreamId;

	memcpy(newpkt->pData, m_dynm->data(), m_dynm->size());

	CDVDDemuxUtils::FreeDemuxPacket(srcPkt);

	return newpkt;
}

uint8_t *DVDDoviStreamMuxer::findStartCode(uint8_t *startp, uint8_t *endp) const
{
	if (startp > (endp - 4)) {
		// there can't be any start code
		return endp;
	}

	for (uint8_t *p = startp; p < endp - 4; p++) {
		if (p[0] == 0 && p[1] == 0) {
			if (p[2] == 1) {
				return p + 3;
			} else if (p[2] == 0 && p[3] == 1) {
				return p + 4;
			}
		}
	}

	// no startcode found
	return endp;
}
