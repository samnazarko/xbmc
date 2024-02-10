/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MPEG4Codec.h"

#include "AMLCodecList.h"
#include "AMLVideoCodecInfo.h"
#include "AMLVideoCodecConstants.h"

#include "utils/log.h"
#include "settings/Settings.h"
#include "ServiceBroker.h"
#include "settings/SettingsComponent.h"

#define DIVX311_CHUNK_HEAD_SIZE 13


using namespace amlogic;

class MPEG4CodecInfo : public AMLVideoCodecInfo
{
public:
	MPEG4CodecInfo() {
		AMLCodecList::getInstance().registerAmlVideoCodec(this);
	}

	std::string getCodecName() const { return "MPEG-4"; }
	bool isSecure() const { return false; }
	bool canPlay(const CDVDStreamInfo &hints) const {
		if (hints.codec != AV_CODEC_ID_MPEG4 && hints.codec != AV_CODEC_ID_MSMPEG4V2 && hints.codec != AV_CODEC_ID_MSMPEG4V3) {
			return false;
		}

		if (hints.width <= CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_USEAMCODECMPEG4)) {
			return false;
		}

		return true;
	}

	AMLVideoCodec *createCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints) {
		return new MPEG4Codec(processInfo);
	}

} mpeg4CodecInfo;


MPEG4Codec::MPEG4Codec(CProcessInfo &processInfo)
	: AMLInsecureVideoCodec(processInfo)
{
}

MPEG4Codec::~MPEG4Codec()
{
}

std::string MPEG4Codec::getFormatName() const
{
	return "am-mpeg4";
}

vformat_t MPEG4Codec::getVideoFormat(const CDVDStreamInfo &hints) const
{
	return VFORMAT_MPEG4;
}

vdec_type_t MPEG4Codec::getVideoCodecType(const CDVDStreamInfo &hints) const
{
	switch (hints.codec_tag) {
		case CODEC_TAG_XVID:
		case CODEC_TAG_xvid:
		case CODEC_TAG_XVIX:
			// xvid
			return VIDEO_DEC_FORMAT_MPEG4_5;

		case CODEC_TAG_COL1:
		case CODEC_TAG_DIV3:
		case CODEC_TAG_MP43:
			// divx3.11
			return VIDEO_DEC_FORMAT_MPEG4_3;

		case CODEC_TAG_DIV4:
		case CODEC_TAG_DIVX:
			// divx4
			return VIDEO_DEC_FORMAT_MPEG4_4;

		case CODEC_TAG_DIV5:
		case CODEC_TAG_DX50:
		case CODEC_TAG_M4S2:
		case CODEC_TAG_FMP4:
			// divx5
			return VIDEO_DEC_FORMAT_MPEG4_5;

		case CODEC_TAG_DIV6:
			// divx6
			return VIDEO_DEC_FORMAT_MPEG4_5;

		case CODEC_TAG_MP4V:
		case CODEC_TAG_RMP4:
		case CODEC_TAG_MPG4:
		case CODEC_TAG_mp4v:
			// mp4
			return VIDEO_DEC_FORMAT_MPEG4_5;

		default:
			return VIDEO_DEC_FORMAT_MPEG4_5;
	}
}

static int divx3_write_header(am_private_t *para, am_packet_t *pkt)
{
	CLog::Log(LOGDEBUG, "divx3_write_header");

	unsigned i = (para->video_width << 12) | (para->video_height & 0xfff);
	unsigned char divx311_add[10] = {
		0x00, 0x00, 0x00, 0x01,
		0x20, 0x00, 0x00, 0x00,
		0x00, 0x00
	};

	divx311_add[5] = (i >> 16) & 0xff;
	divx311_add[6] = (i >> 8) & 0xff;
	divx311_add[7] = i & 0xff;

	memcpy(pkt->hdr->data, divx311_add, sizeof(divx311_add));
	pkt->hdr->size = sizeof(divx311_add);

	pkt->newflag = 1;

	return PLAYER_SUCCESS;
}

static int m4s2_dx50_mp4v_write_header(am_private_t *para, am_packet_t *pkt)
{
    CLog::Log(LOGDEBUG, "m4s2_dx50_mp4v_write_header");

	pkt->hdr->alloc(para->extrasize);
	if (!pkt->hdr->data) {
		CLog::Log(LOGDEBUG, "[m4s2_dx50_mp4v_write_header] NOMEM!");
		return PLAYER_NOMEM;
	}

	pkt->hdr->size = para->extrasize;
	memcpy(pkt->hdr->data, para->extradata, para->extrasize);

	pkt->newflag = 1;

	return PLAYER_SUCCESS;
}

int MPEG4Codec::pre_header_feeding(am_private_t *para, am_packet_t *pkt) const
{
	int ret = PLAYER_SUCCESS;

	if (para->video_codec_type == VIDEO_DEC_FORMAT_MPEG4_3) {
		ret = divx3_write_header(para, pkt);
	} else if (para->video_codec_tag == CODEC_TAG_M4S2 || para->video_codec_tag == CODEC_TAG_DX50 || para->video_codec_tag == CODEC_TAG_mp4v) {
		ret = m4s2_dx50_mp4v_write_header(para, pkt);
	}

	return ret;
}

static int divx3_prefix(am_packet_t *pkt)
{
	const unsigned char divx311_chunk_prefix[DIVX311_CHUNK_HEAD_SIZE] = {
		0x00, 0x00, 0x00, 0x01, 0xb6, 'D', 'I', 'V', 'X', '3', '.', '1', '1'
	};

	pkt->hdr->alloc(DIVX311_CHUNK_HEAD_SIZE + 4);
	if (!pkt->hdr->data) {
		CLog::Log(LOGDEBUG, "[divx3_prefix] NOMEM!");
		return PLAYER_NOMEM;
	}

	memcpy(pkt->hdr->data, divx311_chunk_prefix, DIVX311_CHUNK_HEAD_SIZE);

	pkt->hdr->data[DIVX311_CHUNK_HEAD_SIZE + 0] = (pkt->data_size >> 24) & 0xff;
	pkt->hdr->data[DIVX311_CHUNK_HEAD_SIZE + 1] = (pkt->data_size >> 16) & 0xff;
	pkt->hdr->data[DIVX311_CHUNK_HEAD_SIZE + 2] = (pkt->data_size >> 8) & 0xff;
	pkt->hdr->data[DIVX311_CHUNK_HEAD_SIZE + 3] = pkt->data_size & 0xff;

	pkt->hdr->size = DIVX311_CHUNK_HEAD_SIZE + 4;
	pkt->newflag = 1;

	return PLAYER_SUCCESS;
}

int MPEG4Codec::set_header_info(am_private_t *para) const
{
	if (para->video_codec_type == VIDEO_DEC_FORMAT_MPEG4_3) {
		return divx3_prefix(&para->am_pkt);
	}

	return PLAYER_SUCCESS;
}

void MPEG4Codec::setupVideoCodecParams(aml_generic_param &params) const
{
	AMLInsecureVideoCodec::setupVideoCodecParams(params);

	params.param = (void*) EXTERNAL_PTS;
}
