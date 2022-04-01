/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AMLInsecureVideoCodec.h"

typedef struct mpeg2_sequence mpeg2_sequence;

class amlogic::MPEG12Codec : public amlogic::AMLInsecureVideoCodec
{
private:
	mpeg2_sequence	*m_mpeg2_sequence;
	double			 m_mpeg2_sequence_pts;
	double			 m_aspect_ratio;
	double			 m_framerate;

protected:
	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const;

	virtual void getExtradata(const CDVDStreamInfo &hints, uint8_t *&extradata, unsigned int &extrasize) const;

public:
	MPEG12Codec(CProcessInfo &processInfo, const CDVDStreamInfo &hints);
	virtual ~MPEG12Codec();

	virtual std::string getFormatName() const;
	virtual double getAspectRatio() const;
	virtual CDVDVideoCodec::VCReturn getPicture(VideoPicture *pVideoPicture);

	virtual bool prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts);

	virtual void reset();

};
