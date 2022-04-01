/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "MPEG12Codec.h"

#include "AMLCodecList.h"
#include "AMLVideoCodecInfo.h"
#include "AMLVideoCodecConstants.h"

#include "utils/BitstreamConverter.h"
#include "settings/Settings.h"
#include "ServiceBroker.h"
#include "settings/SettingsComponent.h"

using namespace amlogic;

class MPEG12CodecInfo : public AMLVideoCodecInfo
{
public:
	MPEG12CodecInfo() {
		AMLCodecList::getInstance().registerAmlVideoCodec(this);
	}

	std::string getCodecName() const { return "MPEG-1/MPEG-2"; }
	bool isSecure() const { return false; }
	bool canPlay(const CDVDStreamInfo &hints) const {
		if (hints.codec != AV_CODEC_ID_MPEG1VIDEO && hints.codec != AV_CODEC_ID_MPEG2VIDEO) {
			return false;
		}

		if (hints.width <= CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_USEAMCODECMPEG2)) {
			return false;
		}

		return true;
	}

	AMLVideoCodec *createCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints) {
		return new MPEG12Codec(processInfo, hints);
	}

} mpeg12CodecInfo;


MPEG12Codec::MPEG12Codec(CProcessInfo &processInfo, const CDVDStreamInfo &hints)
	: AMLInsecureVideoCodec(processInfo), m_mpeg2_sequence_pts(0), m_aspect_ratio(hints.aspect),
	  m_framerate(0.0)
{
	m_mpeg2_sequence = new mpeg2_sequence;
	m_mpeg2_sequence->width = hints.width;
	m_mpeg2_sequence->height = hints.height;
	m_mpeg2_sequence->ratio = hints.aspect;
	m_mpeg2_sequence->fps_rate = hints.fpsrate;
	m_mpeg2_sequence->fps_scale = hints.fpsscale;
}

MPEG12Codec::~MPEG12Codec()
{
	if (m_mpeg2_sequence) {
		delete m_mpeg2_sequence, m_mpeg2_sequence = nullptr;
	}
}

std::string MPEG12Codec::getFormatName() const
{
	return "am-mpeg2";
}

vformat_t MPEG12Codec::getVideoFormat(const CDVDStreamInfo &hints) const
{
	return VFORMAT_MPEG12;
}

vdec_type_t MPEG12Codec::getVideoCodecType(const CDVDStreamInfo &hints) const
{
	return VIDEO_DEC_FORMAT_UNKNOW;
}

void MPEG12Codec::reset()
{
	AMLInsecureVideoCodec::reset();

	m_mpeg2_sequence_pts = 0;
}

double MPEG12Codec::getAspectRatio() const
{
	return m_aspect_ratio;
}

CDVDVideoCodec::VCReturn MPEG12Codec::getPicture(VideoPicture *pVideoPicture)
{
	CDVDVideoCodec::VCReturn ret = AMLInsecureVideoCodec::getPicture(pVideoPicture);

	// check for mpeg2 aspect ratio changes
	if (pVideoPicture->pts >= m_mpeg2_sequence_pts) {
		m_aspect_ratio = m_mpeg2_sequence->ratio;
	}

	return ret;
}

bool MPEG12Codec::prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts)
{
	// probe demux for sequence_header_code NAL and
	// decode aspect ratio and frame rate.
	if (CBitstreamConverter::mpeg2_sequence_header(data, size, m_mpeg2_sequence) && (m_mpeg2_sequence->fps_rate > 0) && (m_mpeg2_sequence->fps_scale > 0)) {
		m_mpeg2_sequence_pts = pts;
		if (m_mpeg2_sequence_pts == DVD_NOPTS_VALUE) {
			m_mpeg2_sequence_pts = dts;
		}

		hints.fpsrate = m_mpeg2_sequence->fps_rate;
		hints.fpsscale = m_mpeg2_sequence->fps_scale;

		m_framerate = static_cast<float>(m_mpeg2_sequence->fps_rate) / m_mpeg2_sequence->fps_scale;

		hints.width = m_mpeg2_sequence->width;
		hints.height = m_mpeg2_sequence->height;
		hints.aspect = m_mpeg2_sequence->ratio;

		m_processInfo.SetVideoFps(m_framerate);
		m_processInfo.SetVideoDAR(hints.aspect);
	}

	return true;
}

void MPEG12Codec::getExtradata(const CDVDStreamInfo &hints, uint8_t *&extradata, unsigned int &extrasize) const
{
	extradata = nullptr;
	extrasize = 0;
}
