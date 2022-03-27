/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "H264Codec.h"

#include "AMLCodecList.h"
#include "AMLVideoCodecInfo.h"
#include "AMLVideoCodecConstants.h"

#include "OSMCSecureOS.h"

#include "utils/AMLUtils.h"
#include "utils/BitstreamConverter.h"
#include "utils/log.h"
#include "settings/Settings.h"
#include "ServiceBroker.h"
#include "settings/SettingsComponent.h"

#ifndef CODEC_TAG_AMVC
	#define CODEC_TAG_AMVC  (0x43564d41)
#endif

#ifndef CODEC_TAG_MVC1
	#define CODEC_TAG_MVC1  (0x3143564d)
#endif

using namespace amlogic;

class H264CodecInfo : public AMLVideoCodecInfo
{
public:
	H264CodecInfo() {
		AMLCodecList::getInstance().registerAmlVideoCodec(this);
	}

	std::string getCodecName() const { return "H.264/MVC"; }
	bool isSecure() const { return false; }
	bool canPlay(const CDVDStreamInfo &hints) const {
		if (hints.codec != AV_CODEC_ID_H264) {
			return false;
		}

		int resolutionLimit = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_USEAMCODECH264);
		if (hints.width <= resolutionLimit) {
			CLog::Log(LOGDEBUG, "H264CodecInfo: size check failed {}", resolutionLimit);
			return false;
		}

		switch (hints.profile) {
			case FF_PROFILE_H264_HIGH_10:
			case FF_PROFILE_H264_HIGH_10_INTRA:
			case FF_PROFILE_H264_HIGH_422:
			case FF_PROFILE_H264_HIGH_422_INTRA:
			case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
			case FF_PROFILE_H264_HIGH_444_INTRA:
			case FF_PROFILE_H264_CAVLC_444:
				CLog::Log(LOGDEBUG, "H264CodecInfo: profile {} not supported", hints.profile);
				return false;
		}

		if ((aml_support_h264_4k2k() == AML_NO_H264_4K2K) && (hints.width > 1920 || hints.height > 1088)) {
			// 4K is supported only on Amlogic S802/S812 chip
			return false;
		}

		return true;
	}

	AMLVideoCodec *createCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints) {
		bool requestMvc = hints.codec_tag == CODEC_TAG_AMVC || hints.codec_tag == CODEC_TAG_MVC1;
		bool request4k = !requestMvc && aml_support_h264_4k2k() == AML_HAS_H264_4K2K && (hints.width > 1920 || hints.height > 1088);

		return new H264Codec(processInfo, request4k, requestMvc);
	}

} h264CodecInfo;


H264Codec::H264Codec(CProcessInfo &processInfo, bool request4k, bool requestMvc)
	: AMLInsecureVideoCodec(processInfo), m_is4k(request4k), m_isMvc(requestMvc), m_hasKeyframe(false),
	  m_bitstream(nullptr), m_bitparser(nullptr)
{
}

H264Codec::~H264Codec()
{
	if (m_bitstream) {
		delete m_bitstream, m_bitstream = nullptr;
	}
	if (m_bitparser) {
		delete m_bitparser, m_bitparser = nullptr;
	}
}

std::string H264Codec::getFormatName() const
{
	return m_isMvc ? "am-h264mvc" : "am-h264";
}

vformat_t H264Codec::getVideoFormat(const CDVDStreamInfo &hints) const
{
	return m_isMvc ? VFORMAT_H264MVC : (m_is4k ? VFORMAT_H264_4K2K : VFORMAT_H264);
}

vdec_type_t H264Codec::getVideoCodecType(const CDVDStreamInfo &hints) const
{
	return m_is4k ? VIDEO_DEC_FORMAT_H264_4K2K : VIDEO_DEC_FORMAT_H264;
}

int H264Codec::calculateSleepDuration()
{
	// we need a shorter sleep period for MVC playback to avoid Kodi skipping like hell
	return AMLInsecureVideoCodec::calculateSleepDuration() / (m_isMvc ? 2 : 1);
}

void H264Codec::setupVideoCodecParams(aml_generic_param &params) const
{
	AMLInsecureVideoCodec::setupVideoCodecParams(params);

	params.param = (void*) EXTERNAL_PTS;
	// h264 in an avi file
	if (m_hints.ptsinvalid) {
		params.param = (void*) (EXTERNAL_PTS | SYNC_OUTSIDE);
	}
}

int H264Codec::pre_header_feeding(am_private_t *para, am_packet_t *pkt) const
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

void H264Codec::playbackResume(int speed)
{
	m_libamcodec->setControlMode(TRICKMODE_FFFB);
}

unsigned int H264Codec::getVideoRate(const CDVDStreamInfo &hints) const
{
	unsigned int rate = AMLInsecureVideoCodec::getVideoRate(hints);

	// check for SD h264 content incorrectly reported as 60 fps
	// mp4/avi containers :(
	if (hints.width <= 720 && rate == 1602) {
		CLog::Log(LOGDEBUG, "H264Codec::getVideoRate video_rate exception");
		rate = 0.5 + (float) UNIT_FREQ * 1001 / 24000;
	}

	// check for SD h264 content incorrectly reported as some form of 30 fps
	// mp4/avi containers :(
	if (hints.width <= 720 && rate >= 3200 && rate <= 3210) {
		CLog::Log(LOGDEBUG, "H264Codec::getVideoRate video_rate exception");
		rate = 0.5 + (float) UNIT_FREQ * 1001 / 24000;
	}

	return rate;
}

CDVDVideoCodec::VCReturn H264Codec::getPicture(VideoPicture *pVideoPicture)
{
	CDVDVideoCodec::VCReturn ret = AMLInsecureVideoCodec::getPicture(pVideoPicture);

	if (m_isMvc && ret == CDVDVideoCodec::VC_PICTURE) {
		pVideoPicture->m_3dSubtitleDepth = m_secureOSMC->getMvcSubtitleDepth(pVideoPicture->pts/DVD_TIME_BASE);

		pVideoPicture->stereoMode = m_hints.stereo_mode;

		if (pVideoPicture->stereoMode == "block_lr" && m_processInfo.GetVideoSettings().m_StereoInvert) {
			pVideoPicture->stereoMode = "block_rl";
		} else if (pVideoPicture->stereoMode == "block_rl" && m_processInfo.GetVideoSettings().m_StereoInvert) {
			pVideoPicture->stereoMode = "block_lr";
		}
	}

	return ret;
}

bool H264Codec::openDecoder(CDVDStreamInfo &hints)
{
	if (AMLInsecureVideoCodec::openDecoder(hints)) {
		m_hasKeyframe = false;
		return true;
	}

	return false;
}

void H264Codec::reset()
{
	AMLInsecureVideoCodec::reset();

	m_hasKeyframe = false;
	if (m_bitstream) {
		m_bitstream->ResetStartDecode();
	}
}

bool H264Codec::prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts)
{
	if (m_bitstream == nullptr && m_bitparser == nullptr) {
		// convert h264-avcC to h264-annex-b as h264-avcC
		// under streamers can have issues when seeking.
		if (hints.extradata && *(uint8_t*) hints.extradata == 1) {
			m_bitstream = new CBitstreamConverter;

			m_bitstream->Open(hints.codec, (uint8_t*) hints.extradata, hints.extrasize, true);
			m_bitstream->ResetStartDecode();

			// make sure we do not leak the existing hints.extradata
			free(hints.extradata);

			hints.extrasize = m_bitstream->GetExtraSize();
			hints.extradata = malloc(hints.extrasize);
			memcpy(hints.extradata, m_bitstream->GetExtraData(), hints.extrasize);
		} else {
			m_bitparser = new CBitstreamParser();
			m_bitparser->Open();
		}
	}

    if (m_bitstream) {
		if (!m_bitstream->Convert(data, size)) {
			return false;
		}

		if (!m_bitstream->CanStartDecode()) {
			CLog::Log(LOGDEBUG, "H264Codec::prepareFrame waiting for keyframe (bitstream)");
			return false;
		}

		data = m_bitstream->GetConvertBuffer();
		size = m_bitstream->GetConvertSize();
	} else if (!m_hasKeyframe && m_bitparser) {
		if (!m_bitparser->CanStartDecode(data, size)) {
			CLog::Log(LOGDEBUG, "H264Codec::prepareFrame waiting for keyframe (bitparser)");
			return false;
		} else {
			m_hasKeyframe = true;
		}
	}

    return true;
}
