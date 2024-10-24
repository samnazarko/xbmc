/*
 *	(c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DVDStreamMuxer.h"

#include <memory>

class CDVDInputStreamBluray;
class FFmpegExtraData;

class DVDMvcStreamMuxer : public DVDStreamMuxer
{
private:
	std::shared_ptr<CDVDInputStreamBluray> m_bdInputStream;

	FFmpegExtraData mergeExtraData(FFmpegExtraData mainExtraData, FFmpegExtraData extensionExtraData);

public:
	DVDMvcStreamMuxer(CDemuxStreamVideo *main, CDemuxStreamVideo *extension);
	DVDMvcStreamMuxer(CDemuxStreamVideo *main, const std::shared_ptr<CDVDInputStreamBluray> &bdInputStream);
	virtual ~DVDMvcStreamMuxer();

	virtual DemuxPacket *addPacket(DemuxPacket *pkt);
};
