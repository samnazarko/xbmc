/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */


#include "VC1Codec.h"

#include "AMLCodecList.h"
#include "AMLVideoCodecInfo.h"
#include "AMLVideoCodecConstants.h"

#include "utils/log.h"
#include "utils/SysfsUtils.h"

using namespace amlogic;

class VC1CodecInfo : public AMLVideoCodecInfo
{
public:
	VC1CodecInfo() {
		AMLCodecList::getInstance().registerAmlVideoCodec(this);
	}

	std::string getCodecName() const { return "VC-1"; }
	bool isSecure() const { return false; }
	bool canPlay(const CDVDStreamInfo &hints) const {
		return hints.codec == AV_CODEC_ID_VC1 || hints.codec == AV_CODEC_ID_WMV3;
	}

	AMLVideoCodec *createCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints) {
		return new VC1Codec(processInfo, hints.codec == AV_CODEC_ID_VC1 ? "am-vc1" : "am-wmv3");
	}

} vc1CodecInfo;

VC1Codec::VC1Codec(CProcessInfo &processInfo, std::string formatName)
	: AMLInsecureVideoCodec(processInfo), m_formatName(formatName)
{
}

VC1Codec::~VC1Codec()
{
}

std::string VC1Codec::getFormatName() const
{
	return m_formatName;
}

vformat_t VC1Codec::getVideoFormat(const CDVDStreamInfo &hints) const
{
	return VFORMAT_VC1;
}

vdec_type_t VC1Codec::getVideoCodecType(const CDVDStreamInfo &hints) const
{
	switch (hints.codec_tag) {
		case CODEC_TAG_WMV3:
			// wmv3
			return VIDEO_DEC_FORMAT_WMV3;

		case CODEC_TAG_VC_1:
		case CODEC_TAG_WVC1:
		case CODEC_TAG_WMVA:
			// vc1
			return VIDEO_DEC_FORMAT_WVC1;

		default:
			return VIDEO_DEC_FORMAT_WVC1;
	}
}

void VC1Codec::setupVideoCodecParams(aml_generic_param &params) const
{
	AMLInsecureVideoCodec::setupVideoCodecParams(params);

	if (m_hints.ptsinvalid) {
		params.param = (void*) UNSTABLE_PTS;
	}
}

bool VC1Codec::prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts)
{
	char fcm;
	std::string FCM;
	bool interlaced = true;
	unsigned int i;
	int progressive, frameinterlace, hdr_start = 0;

	if ( !((data) && (size >= 4)
		&& (data[0] == 0) && (data[1] == 0)
		&& (data[2] == 1) && (data[3] == 0xd || data[3] == 0xf)))
	{
		CLog::Log(LOGDEBUG, "VC1Codec::prepareFrame No start code at start of packet");
		// nothing to do here
		return true;
	}

	if (data[3] == 0xf && size > 9) {
		interlaced = data[9] & 0x40;
		// sequence start - look for the first frame
		for (i=4; i < size - 5; i++)
		{
			if ((data[i] << 24 | data[i+1] << 16 | data[i+2] << 8 | data[i+3]) == 0x0000010d)
			{
				hdr_start = i + 4;
				break;
			}
		}
		if (hdr_start == 0)
			return true;
	}
	else if (data[3] == 0xd  && size > 4)
		hdr_start = 4;

	fcm = 0;
	if (interlaced)
		fcm = data[hdr_start] >> 6;
	if (fcm < 0x2)
		fcm = 0;

	if (fcm == 0)
		FCM = "Progressive";
	else if (fcm == 0x2)
		FCM = "Frame interlace";
	else
		FCM = "Field interlace";

	if (hdr_start > 4)
	{
		if ( SysfsUtils::GetInt("/sys/module/amvdec_vc1/parameters/force_frameint", frameinterlace))
			return true;
		if (m_processInfo.GetVideoSettings().m_InterlaceMethod == VS_INTERLACEMETHOD_DEINTERLACE)
		{
			progressive = 0;
			FCM = "Field interlace";
		}
		else if (m_processInfo.GetVideoSettings().m_InterlaceMethod == VS_INTERLACEMETHOD_NONE)
		{
			progressive = 1;
			FCM = "Frame interlace/progressive";
		}
		else
			progressive = fcm < 3 ? 1 : 0;

		if (frameinterlace != progressive)
		{
			SysfsUtils::SetInt("/sys/module/amvdec_vc1/parameters/force_frameint", progressive);
			CLog::Log(LOGDEBUG, "VC1Codec::prepareFrame deinterlace changed to {}", FCM);
		}
	}

	return true;
}

void VC1Codec::getDeinterlacingMethods(std::list<EINTERLACEMETHOD> &methods) const
{
	/* add deinterlace options */
	methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_NONE);
	methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_DEINTERLACE);
	methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_AUTO);
	m_processInfo.SetDeinterlacingMethodDefault(EINTERLACEMETHOD::VS_INTERLACEMETHOD_DEINTERLACE);
}

int VC1Codec::write_header(am_private_t *para, am_packet_t *pkt) const
{
	if (pkt->hdr->isEmpty()) {
		return PLAYER_SUCCESS;
	}

	if (para->video_format == VFORMAT_VC1 && para->video_codec_type == VIDEO_DEC_FORMAT_WVC1) {
		if ((pkt->data) && (pkt->data_size >= 4)
			&& (pkt->data[0] == 0) && (pkt->data[1] == 0)
			&& (pkt->data[2] == 1) && (pkt->data[3] == 0xd || pkt->data[3] == 0xf)) {
			return PLAYER_SUCCESS;
		}
	}

	return AMLInsecureVideoCodec::write_header(para, pkt);
}

int VC1Codec::set_header_info(am_private_t *para) const
{
	am_packet_t *pkt = &para->am_pkt;

	pkt->hdr->free();

	if (para->video_codec_type == VIDEO_DEC_FORMAT_WMV3) {
		unsigned i, check_sum = 0, data_len = 0;

		if (pkt->avpkt.flags) {
			pkt->hdr->alloc(para->extrasize + 26 + 22);
			if (pkt->hdr->data == nullptr) {
				return PLAYER_FAILED;
			}

			pkt->hdr->data[0] = 0;
			pkt->hdr->data[1] = 0;
			pkt->hdr->data[2] = 1;
			pkt->hdr->data[3] = 0x10;

			data_len = para->extrasize + 4;
			pkt->hdr->data[4] = 0;
			pkt->hdr->data[5] = (data_len >> 16) & 0xff;
			pkt->hdr->data[6] = 0x88;
			pkt->hdr->data[7] = (data_len >> 8) & 0xff;
			pkt->hdr->data[8] = data_len & 0xff;
			pkt->hdr->data[9] = 0x88;

			pkt->hdr->data[10] = 0xff;
			pkt->hdr->data[11] = 0xff;
			pkt->hdr->data[12] = 0x88;
			pkt->hdr->data[13] = 0xff;
			pkt->hdr->data[14] = 0xff;
			pkt->hdr->data[15] = 0x88;

			for (i = 4; i < 16; i++) {
				check_sum += pkt->hdr->data[i];
			}

			pkt->hdr->data[16] = (check_sum >> 8) & 0xff;
			pkt->hdr->data[17] = check_sum & 0xff;
			pkt->hdr->data[18] = 0x88;
			pkt->hdr->data[19] = (check_sum >> 8) & 0xff;
			pkt->hdr->data[20] = check_sum & 0xff;
			pkt->hdr->data[21] = 0x88;

			pkt->hdr->data[22] = (para->video_width >> 8) & 0xff;
			pkt->hdr->data[23] = para->video_width & 0xff;
			pkt->hdr->data[24] = (para->video_height >> 8) & 0xff;
			pkt->hdr->data[25] = para->video_height & 0xff;

			memcpy(pkt->hdr->data + 26, para->extradata, para->extrasize);

			check_sum = 0;
			data_len = para->extrasize + 26;
		} else {
			pkt->hdr->alloc(22);
			if (pkt->hdr->data == nullptr) {
				return PLAYER_FAILED;
			}
		}

		pkt->hdr->data[data_len + 0] = 0;
		pkt->hdr->data[data_len + 1] = 0;
		pkt->hdr->data[data_len + 2] = 1;
		pkt->hdr->data[data_len + 3] = 0xd;

		pkt->hdr->data[data_len + 4] = 0;
		pkt->hdr->data[data_len + 5] = (pkt->data_size >> 16) & 0xff;
		pkt->hdr->data[data_len + 6] = 0x88;
		pkt->hdr->data[data_len + 7] = (pkt->data_size >> 8) & 0xff;
		pkt->hdr->data[data_len + 8] = pkt->data_size & 0xff;
		pkt->hdr->data[data_len + 9] = 0x88;

		pkt->hdr->data[data_len + 10] = 0xff;
		pkt->hdr->data[data_len + 11] = 0xff;
		pkt->hdr->data[data_len + 12] = 0x88;
		pkt->hdr->data[data_len + 13] = 0xff;
		pkt->hdr->data[data_len + 14] = 0xff;
		pkt->hdr->data[data_len + 15] = 0x88;

		for (i = data_len + 4; i < data_len + 16; i++) {
			check_sum += pkt->hdr->data[i];
		}

		pkt->hdr->data[data_len + 16] = (check_sum >> 8) & 0xff;
		pkt->hdr->data[data_len + 17] = check_sum & 0xff;
		pkt->hdr->data[data_len + 18] = 0x88;
		pkt->hdr->data[data_len + 19] = (check_sum >> 8) & 0xff;
		pkt->hdr->data[data_len + 20] = check_sum & 0xff;
		pkt->hdr->data[data_len + 21] = 0x88;

		pkt->hdr->size = data_len + 22;
		pkt->newflag = 1;
	} else if (para->video_codec_type == VIDEO_DEC_FORMAT_WVC1) {
		pkt->hdr->alloc(4);
		if (pkt->hdr->data == nullptr) {
			CLog::Log(LOGDEBUG, "[wvc1_prefix] NOMEM!");
			return PLAYER_FAILED;
		}

		pkt->hdr->data[0] = 0;
		pkt->hdr->data[1] = 0;
		pkt->hdr->data[2] = 1;
		pkt->hdr->data[3] = 0xd;
		pkt->hdr->size = 4;
		pkt->newflag = 1;
	}

	return PLAYER_SUCCESS;
}

void VC1Codec::wmv3_set_header(am_private_t *para, am_packet_t *pkt) const
{
	CLog::Log(LOGDEBUG, "wmv3_set_header");

	unsigned i, check_sum = 0;
	unsigned data_len = para->extrasize + 4;

	pkt->hdr->data[0] = 0;
	pkt->hdr->data[1] = 0;
	pkt->hdr->data[2] = 1;
	pkt->hdr->data[3] = 0x10;

	pkt->hdr->data[4] = 0;
	pkt->hdr->data[5] = (data_len >> 16) & 0xff;
	pkt->hdr->data[6] = 0x88;
	pkt->hdr->data[7] = (data_len >> 8) & 0xff;
	pkt->hdr->data[8] = data_len & 0xff;
	pkt->hdr->data[9] = 0x88;

	pkt->hdr->data[10] = 0xff;
	pkt->hdr->data[11] = 0xff;
	pkt->hdr->data[12] = 0x88;
	pkt->hdr->data[13] = 0xff;
	pkt->hdr->data[14] = 0xff;
	pkt->hdr->data[15] = 0x88;

	for (i = 4; i < 16; i++) {
		check_sum += pkt->hdr->data[i];
	}

	pkt->hdr->data[16] = (check_sum >> 8) & 0xff;
	pkt->hdr->data[17] = check_sum & 0xff;
	pkt->hdr->data[18] = 0x88;
	pkt->hdr->data[19] = (check_sum >> 8) & 0xff;
	pkt->hdr->data[20] = check_sum & 0xff;
	pkt->hdr->data[21] = 0x88;

	pkt->hdr->data[22] = (para->video_width >> 8) & 0xff;
	pkt->hdr->data[23] = para->video_width & 0xff;
	pkt->hdr->data[24] = (para->video_height >> 8) & 0xff;
	pkt->hdr->data[25] = para->video_height & 0xff;

	memcpy(pkt->hdr->data + 26, para->extradata, para->extrasize);
	pkt->hdr->size = para->extrasize + 26;
	pkt->newflag = 1;
}

void VC1Codec::wvc1_set_header(am_private_t *para, am_packet_t *pkt) const
{
    CLog::Log(LOGDEBUG, "wvc1_set_header");

    memcpy(pkt->hdr->data, para->extradata + 1, para->extrasize - 1);
    pkt->hdr->size = para->extrasize - 1;
    pkt->newflag = 1;
}

int VC1Codec::pre_header_feeding(am_private_t *para, am_packet_t *pkt) const
{
	if (para->video_codec_tag == CODEC_TAG_WMV3) {
		CLog::Log(LOGDEBUG, "CODEC_TAG_WMV3 == para->video_codec_tag");
		wmv3_set_header(para, pkt);
	} else if ((CODEC_TAG_WVC1 == para->video_codec_tag) || (CODEC_TAG_VC_1 == para->video_codec_tag) || (CODEC_TAG_WMVA == para->video_codec_tag)) {
		if (para->extrasize > 4 && !*para->extradata && !*(para->extradata + 1) && *(para->extradata + 2) == 0x01 && *(para->extradata + 3) == 0x0f
			&& ((*(para->extradata + 4) & 0x03) == 0x03)) {
			CLog::Log(LOGDEBUG, "CODEC_TAG_WVC1 == para->video_codec_tag, using wmv3_write_header");
			wmv3_set_header(para, pkt);
		} else {
			CLog::Log(LOGDEBUG, "CODEC_TAG_WVC1 == para->video_codec_tag");
			wvc1_set_header(para, pkt);
		}
	}

	return PLAYER_SUCCESS;
}
