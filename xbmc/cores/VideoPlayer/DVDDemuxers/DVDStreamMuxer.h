/*
 *	(c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <queue>

class DemuxPacket;
class CDemuxStreamVideo;

class DVDStreamMuxer
{
private:
	CDemuxStreamVideo *m_main;
	CDemuxStreamVideo *m_extension;

	std::queue<DemuxPacket *> m_mainQueue;
	std::queue<DemuxPacket *> m_extensionQueue;

protected:
	virtual DemuxPacket *mergePackets(DemuxPacket *srcPkt, DemuxPacket *extPkt);
	virtual DemuxPacket *getNextPacket();

	virtual bool isMainPacket(DemuxPacket *pkt) const;
	virtual bool isExtensionPacket(DemuxPacket *pkt) const;

	virtual inline void pushMainQueue(DemuxPacket *pkt) { m_mainQueue.push(pkt); }
	virtual inline void pushExtensionQueue(DemuxPacket *pkt) { m_extensionQueue.push(pkt); }
	virtual inline int mainQueueSize() { return m_mainQueue.size(); }
	virtual inline int extensionQueueSize() { return m_extensionQueue.size(); }

public:
	DVDStreamMuxer(CDemuxStreamVideo *main, CDemuxStreamVideo *extension);
	virtual ~DVDStreamMuxer();

	virtual DemuxPacket *addPacket(DemuxPacket *pkt);
	virtual void flush();
};
