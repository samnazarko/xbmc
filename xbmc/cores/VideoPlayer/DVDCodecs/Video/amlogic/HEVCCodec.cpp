/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "HEVCCodec.h"

#include "AMLCodecList.h"
#include "AMLVideoCodecInfo.h"
#include "AMLVideoCodecConstants.h"

#include "utils/AMLUtils.h"
#include "utils/BitstreamConverter.h"
#include "utils/log.h"

using namespace amlogic;

class HEVCCodecInfo : public AMLVideoCodecInfo
{
public:
	HEVCCodecInfo() {
		AMLCodecList::getInstance().registerAmlVideoCodec(this);
	}

	std::string getCodecName() const { return "HEVC"; }
	bool isSecure() const { return false; }
	bool canPlay(const CDVDStreamInfo &hints) const {
		if (hints.codec != AV_CODEC_ID_HEVC) {
			return false;
		}

		if (aml_support_hevc()) {
			if (!aml_support_hevc_4k2k() && (hints.width > 1920 || hints.height > 1088)) {
				// 4K HEVC is supported only on Amlogic S812 chip
				return false;
			}
		} else {
			// HEVC supported only on S805 and S812.
			return false;
		}

		if (hints.profile == FF_PROFILE_HEVC_MAIN_10 && !aml_support_hevc_10bit()) {
			return false;
		}

		return true;
	}

	AMLVideoCodec *createCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints) {
		return new HEVCCodec(processInfo, hints);
	}

} hevcCodecInfo;


HEVCCodec::HEVCCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints)
	: DVCodec(processInfo, hints), m_bitstream(nullptr)
{
}

HEVCCodec::~HEVCCodec()
{
	if (m_bitstream) {
		delete m_bitstream, m_bitstream = nullptr;
	}
}

std::string HEVCCodec::getFormatName() const
{
	return "am-h265";
}

vformat_t HEVCCodec::getVideoFormat(const CDVDStreamInfo &hints) const
{
	return VFORMAT_HEVC;
}

vdec_type_t HEVCCodec::getVideoCodecType(const CDVDStreamInfo &hints) const
{
	return VIDEO_DEC_FORMAT_HEVC;
}

void HEVCCodec::setupVideoCodecParams(aml_generic_param &params) const
{
	DVCodec::setupVideoCodecParams(params);

	params.decoder_type = DECODER_TYPE_FRAME_MODE;
	params.param = (void*) EXTERNAL_PTS;
	if (m_hints.ptsinvalid) {
		params.param = (void*) (EXTERNAL_PTS | SYNC_OUTSIDE);
	}
}

int HEVCCodec::pre_header_feeding(am_private_t *para, am_packet_t *pkt) const
{
	if (para->extradata) {
		pkt->hdr->alloc(para->extrasize);
		if (!pkt->hdr->data) {
			return PLAYER_NOMEM;
		}

		memcpy(pkt->hdr->data, para->extradata, para->extrasize);
		pkt->hdr->size = para->extrasize;
	}

	pkt->newflag = 1;

	return PLAYER_SUCCESS;
}

bool HEVCCodec::prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts)
{
	if (m_bitstream == nullptr) {
		m_bitstream = new CBitstreamConverter();

		m_bitstream->Open(hints.codec, (uint8_t*)hints.extradata.GetData(), hints.extradata.GetSize(), true);

		hints.extradata = FFmpegExtraData(m_bitstream->GetExtraData(), m_bitstream->GetExtraSize());
	}

	if (!m_bitstream->Convert(data, size)) {
		return false;
	}

	if (!m_bitstream->CanStartDecode()) {
		CLog::Log(LOGDEBUG, "HEVCCodec::prepareFrame waiting for keyframe (bitstream)");
		return false;
	}

	data = m_bitstream->GetConvertBuffer();
	size = m_bitstream->GetConvertSize();

	return true;
}
