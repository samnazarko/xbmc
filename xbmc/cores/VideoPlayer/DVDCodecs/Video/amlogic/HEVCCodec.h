/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DVCodec.h"

class CBitstreamConverter;

class amlogic::HEVCCodec : public amlogic::DVCodec
{
private:
	CBitstreamConverter	*m_bitstream;

protected:
	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const;

	virtual void setupVideoCodecParams(aml_generic_param &params) const;

	virtual int pre_header_feeding(am_private_t *para, am_packet_t *pkt) const;

public:
	HEVCCodec(CProcessInfo &processInfo);
	virtual ~HEVCCodec();

	virtual std::string getFormatName() const;

	virtual bool prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts);
};
