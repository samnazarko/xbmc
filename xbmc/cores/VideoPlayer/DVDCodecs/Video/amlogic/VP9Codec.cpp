/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VP9Codec.h"

#include "AMLCodecList.h"
#include "AMLVideoCodecInfo.h"
#include "AMLVideoCodecConstants.h"

#include "utils/AMLUtils.h"
#include "utils/log.h"

using namespace amlogic;

class VP9CodecInfo : public AMLVideoCodecInfo
{
public:
	VP9CodecInfo() {
		AMLCodecList::getInstance().registerAmlVideoCodec(this);
	}

	std::string getCodecName() const { return "VP9"; }
	bool isSecure() const { return false; }
	bool canPlay(const CDVDStreamInfo &hints) const {
		if (hints.codec != AV_CODEC_ID_VP9) {
			return false;
		}

		if (!aml_support_vp9()) {
			return false;
		}

		return true;
	}

	AMLVideoCodec *createCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints) {
		return new VP9Codec(processInfo);
	}

} vp9CodecInfo;


VP9Codec::VP9Codec(CProcessInfo &processInfo)
	: AMLInsecureVideoCodec(processInfo)
{
}

VP9Codec::~VP9Codec()
{
}

std::string VP9Codec::getFormatName() const
{
	return "am-vp9";
}

vformat_t VP9Codec::getVideoFormat(const CDVDStreamInfo &hints) const
{
	return VFORMAT_VP9;
}

vdec_type_t VP9Codec::getVideoCodecType(const CDVDStreamInfo &hints) const
{
	return VIDEO_DEC_FORMAT_VP9;
}

int VP9Codec::set_header_info(am_private_t *para) const
{
	am_packet_t *pkt = &para->am_pkt;
	int dsize = pkt->data_size;
	unsigned char *buf = pkt->data;
	unsigned char marker;
	int frame_number;
	int cur_frame, cur_mag, mag, index_sz, offset[9], size[8], tframesize[9];
	int mag_ptr;
	int ret;
	unsigned char *old_header = NULL;
	int total_datasize = 0;

	pkt->avpkt.data = pkt->data;
	pkt->avpkt.size = pkt->data_size;

	if (buf == NULL) {
		return PLAYER_SUCCESS; /*something error. skip add header*/
	}

	marker = buf[dsize - 1];

	if ((marker & 0xe0) == 0xc0) {
		frame_number = (marker & 0x7) + 1;
		mag = ((marker >> 3) & 0x3) + 1;
		index_sz = 2 + mag * frame_number;
		CLog::Log(LOGDEBUG, " frame_number : {}, mag : {}; index_sz : {}\n", frame_number, mag, index_sz);

		offset[0] = 0;
		mag_ptr = dsize - mag * frame_number - 2;
		if (buf[mag_ptr] != marker) {
			CLog::Log(LOGDEBUG, " Wrong marker2 : 0x{:X} --> 0x{:X}\n", marker, buf[mag_ptr]);
			return PLAYER_SUCCESS;
		}

		mag_ptr++;

		for (cur_frame = 0; cur_frame < frame_number; cur_frame++) {
			size[cur_frame] = 0; // or size[0] = bytes_in_buffer - 1; both OK

			for (cur_mag = 0; cur_mag < mag; cur_mag++) {
				size[cur_frame] = size[cur_frame] | (buf[mag_ptr] << (cur_mag * 8));
				mag_ptr++;
			}

			offset[cur_frame + 1] = offset[cur_frame] + size[cur_frame];

			if (cur_frame == 0) {
				tframesize[cur_frame] = size[cur_frame];
			} else {
				tframesize[cur_frame] = tframesize[cur_frame - 1] + size[cur_frame];
			}

			total_datasize += size[cur_frame];
		}
	} else {
		frame_number = 1;
		offset[0] = 0;
		size[0] = dsize; // or size[0] = bytes_in_buffer - 1; both OK
		total_datasize += dsize;
		tframesize[0] = dsize;
	}

	if (total_datasize > dsize) {
		CLog::Log(LOGDEBUG, "DATA overflow : 0x{:X} --> 0x{:X}\n", total_datasize, dsize);
		return PLAYER_SUCCESS;
	}

	if (frame_number >= 1) {
		/*
		 * if only one frame, can used headers.
		 */
		int need_more = total_datasize + frame_number * 16 - dsize;

		av_buffer_unref(&pkt->avpkt.buf);
		ret = av_grow_packet(&(pkt->avpkt), need_more);
		if (ret < 0) {
			CLog::Log(LOGDEBUG, "ERROR!!! grow_packet for apk failed.!!!\n");
			return ret;
		}

		pkt->data = pkt->avpkt.data;
		pkt->data_size = pkt->avpkt.size;
	}

	for (cur_frame = frame_number - 1; cur_frame >= 0; cur_frame--) {
		AVPacket *avpkt = &(pkt->avpkt);
		int framesize = size[cur_frame];
		int oldframeoff = tframesize[cur_frame] - framesize;
		int outheaderoff = oldframeoff + cur_frame * 16;
		uint8_t *fdata = avpkt->data + outheaderoff;
		uint8_t *old_framedata = avpkt->data + oldframeoff;

		memmove(fdata + 16, old_framedata, framesize);
		framesize += 4;/*add 4. for shift.....*/

		/*add amlogic frame headers.*/
		fdata[0] = (framesize >> 24) & 0xff;
		fdata[1] = (framesize >> 16) & 0xff;
		fdata[2] = (framesize >> 8) & 0xff;
		fdata[3] = (framesize >> 0) & 0xff;
		fdata[4] = ((framesize >> 24) & 0xff) ^ 0xff;
		fdata[5] = ((framesize >> 16) & 0xff) ^ 0xff;
		fdata[6] = ((framesize >> 8) & 0xff) ^ 0xff;
		fdata[7] = ((framesize >> 0) & 0xff) ^ 0xff;
		fdata[8] = 0;
		fdata[9] = 0;
		fdata[10] = 0;
		fdata[11] = 1;
		fdata[12] = 'A';
		fdata[13] = 'M';
		fdata[14] = 'L';
		fdata[15] = 'V';
		framesize -= 4;/*del 4 to real framesize for check.....*/

		if (!old_header) {
			// do nothing
		} else if (old_header > fdata + 16 + framesize) {
			CLog::Log(LOGDEBUG, "data has gaps,set to 0\n");
			memset(fdata + 16 + framesize, 0, (old_header - fdata + 16 + framesize));
		} else if (old_header < fdata + 16 + framesize) {
			CLog::Log(LOGDEBUG, "ERROR!!! data over writed!!!! over write {}\n", fdata + 16 + framesize - old_header);
		}

		old_header = fdata;
	}

	return PLAYER_SUCCESS;
}

bool VP9Codec::handleMasteringMetadata(const CDVDStreamInfo &hints) const
{
	if (!AMLInsecureVideoCodec::handleMasteringMetadata(hints)) {
		return false;
	}

	enableCustomMasterDataDisplay();

	return true;
}

void VP9Codec::setupVideoCodecParams(aml_generic_param &params) const
{
	AMLInsecureVideoCodec::setupVideoCodecParams(params);

	params.decoder_type = DECODER_TYPE_FRAME_MODE;
	params.param = (void*) EXTERNAL_PTS;
	if (m_hints.ptsinvalid) {
		params.param = (void*) (EXTERNAL_PTS | SYNC_OUTSIDE);
	}
}
