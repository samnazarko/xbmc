/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AMLInsecureVideoCodec.h"

class CBitstreamConverter;
class CBitstreamParser;

class amlogic::H264Codec : public amlogic::AMLInsecureVideoCodec
{
private:
	bool	m_is4k;
	bool	m_isMvc;
	bool	m_hasKeyframe;

	CBitstreamConverter	*m_bitstream;
	CBitstreamParser	*m_bitparser;

protected:
	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const;
	virtual unsigned int getVideoRate(const CDVDStreamInfo &hints) const;

	virtual void setupVideoCodecParams(aml_generic_param &params) const;

	virtual int pre_header_feeding(am_private_t *para, am_packet_t *pkt) const;

	virtual void playbackResume(int speed);

public:
	H264Codec(CProcessInfo &processInfo, bool request4k, bool requestMvc);
	virtual ~H264Codec();

	virtual std::string getFormatName() const;

	virtual bool openDecoder(CDVDStreamInfo &hints);

	virtual CDVDVideoCodec::VCReturn getPicture(VideoPicture *pVideoPicture);

	virtual bool prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts);

	virtual void reset();
};
