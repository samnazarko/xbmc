/*
 *	(c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDStreamMuxer.h"

#include "DVDDemux.h"
#include "DVDDemuxUtils.h"

#include "utils/log.h"


DVDStreamMuxer::DVDStreamMuxer(CDemuxStreamVideo *main, CDemuxStreamVideo *extension)
	: m_main(main), m_extension(extension)
{
}

DVDStreamMuxer::~DVDStreamMuxer()
{
	flush();
}

bool DVDStreamMuxer::isMainPacket(DemuxPacket *pkt) const
{
	return pkt->iStreamId == m_main->uniqueId;
}

bool DVDStreamMuxer::isExtensionPacket(DemuxPacket *pkt) const
{
	return pkt->iStreamId == m_extension->uniqueId;
}

DemuxPacket *DVDStreamMuxer::addPacket(DemuxPacket *pkt)
{
	if (isMainPacket(pkt)) {
		pushMainQueue(pkt);
	} else if (isExtensionPacket(pkt)) {
		pushExtensionQueue(pkt);
	} else {
		// not for us
		return pkt;
	}

	return getNextPacket();
}

DemuxPacket *DVDStreamMuxer::getNextPacket()
{
	while (!m_mainQueue.empty() && !m_extensionQueue.empty()) {
		DemuxPacket *mainp = m_mainQueue.front();
		DemuxPacket *extp = m_extensionQueue.front();

		double tsM = mainp->dts != DVD_NOPTS_VALUE ? mainp->dts : mainp->pts;
		double tsE = extp->dts != DVD_NOPTS_VALUE ? extp->dts : extp->pts;

		if (tsM == tsE) {
			m_mainQueue.pop();
			m_extensionQueue.pop();

			return mergePackets(mainp, extp);
		} else if (tsM > tsE) {
			CDVDDemuxUtils::FreeDemuxPacket(extp);
			m_extensionQueue.pop();
		} else {
			CDVDDemuxUtils::FreeDemuxPacket(mainp);
			m_mainQueue.pop();
		}
	}

	return CDVDDemuxUtils::AllocateDemuxPacket(0);
}

DemuxPacket *DVDStreamMuxer::mergePackets(DemuxPacket *srcPkt, DemuxPacket *extPkt)
{
	DemuxPacket *newpkt = CDVDDemuxUtils::AllocateDemuxPacket(srcPkt->iSize + extPkt->iSize);

	newpkt->iSize = srcPkt->iSize + extPkt->iSize;
	newpkt->pts = srcPkt->pts;
	newpkt->dts = srcPkt->dts;
	newpkt->duration = srcPkt->duration;
	newpkt->iGroupId = srcPkt->iGroupId;
	newpkt->iStreamId = srcPkt->iStreamId;

	memcpy(newpkt->pData, srcPkt->pData, srcPkt->iSize);
	memcpy(newpkt->pData + srcPkt->iSize, extPkt->pData, extPkt->iSize);

	CDVDDemuxUtils::FreeDemuxPacket(srcPkt);
	CDVDDemuxUtils::FreeDemuxPacket(extPkt);

	return newpkt;
}

void DVDStreamMuxer::flush()
{
	while (!m_mainQueue.empty()) {
		CDVDDemuxUtils::FreeDemuxPacket(m_mainQueue.front());
		m_mainQueue.pop();
	}

	while (!m_extensionQueue.empty()) {
		CDVDDemuxUtils::FreeDemuxPacket(m_extensionQueue.front());
		m_extensionQueue.pop();
	}
}
