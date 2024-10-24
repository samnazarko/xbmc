/*
 *	(c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDMvcStreamMuxer.h"

#include "DVDDemux.h"

#include "cores/VideoPlayer/Interface/DemuxPacket.h"
#include "DVDInputStreams/DVDInputStreamBluray.h"
#include "utils/log.h"


DVDMvcStreamMuxer::DVDMvcStreamMuxer(CDemuxStreamVideo *main, CDemuxStreamVideo *extension)
	: DVDStreamMuxer(main, extension), m_bdInputStream(nullptr)
{
	main->extraData = mergeExtraData(main->extraData, extension->extraData);
	main->codec_fourcc = MKTAG('A', 'M', 'V', 'C');

    // use lr if we don't know what the stereo mode is
	if (main->stereo_mode.empty()) {
		main->stereo_mode = "block_lr";
	}

	extension->disabled = true;
}

DVDMvcStreamMuxer::DVDMvcStreamMuxer(CDemuxStreamVideo *main, const std::shared_ptr<CDVDInputStreamBluray> &bdInputStream)
	: DVDStreamMuxer(main, nullptr), m_bdInputStream(bdInputStream)
{
	main->extraData = mergeExtraData(main->extraData, bdInputStream->GetExtensionExtraData());
	main->codec_fourcc = MKTAG('A', 'M', 'V', 'C');
	main->stereo_mode = bdInputStream->AreEyesFlipped() ? "block_rl" : "block_lr";
}

DVDMvcStreamMuxer::~DVDMvcStreamMuxer()
{
}

FFmpegExtraData DVDMvcStreamMuxer::mergeExtraData(FFmpegExtraData mainExtraData, FFmpegExtraData extensionExtraData)
{
	if (!mainExtraData && !extensionExtraData) {
		return mainExtraData;
	}

	uint8_t *ed = reinterpret_cast<uint8_t *>(av_mallocz(mainExtraData.GetSize() + extensionExtraData.GetSize() + AV_INPUT_BUFFER_PADDING_SIZE));

	memcpy(ed, mainExtraData.GetData(), mainExtraData.GetSize());
	memcpy(ed + mainExtraData.GetSize(), extensionExtraData.GetData(), extensionExtraData.GetSize());

	return FFmpegExtraData(ed, mainExtraData.GetSize() + extensionExtraData.GetSize());
}

DemuxPacket *DVDMvcStreamMuxer::addPacket(DemuxPacket *pkt)
{
	if (m_bdInputStream == nullptr) {
		return DVDStreamMuxer::addPacket(pkt);
	}

	if (isMainPacket(pkt)) {
		pushMainQueue(pkt);

		while (extensionQueueSize() < 5) {
			pkt = m_bdInputStream->ReadExtensionPacket();

			if (pkt == nullptr) {
				break;
			}

			pushExtensionQueue(pkt);
		}
	} else {
		return pkt;
	}

	return getNextPacket();
}
