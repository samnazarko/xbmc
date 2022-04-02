/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AMLInsecureVideoCodec.h"

class amlogic::MPEG4Codec : public amlogic::AMLInsecureVideoCodec
{
protected:
	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const;

	virtual int pre_header_feeding(am_private_t *para, am_packet_t *pkt) const;
	virtual int set_header_info(am_private_t *para) const;

	virtual void setupVideoCodecParams(aml_generic_param &params) const;

public:
	MPEG4Codec(CProcessInfo &processInfo);
	virtual ~MPEG4Codec();

	virtual std::string getFormatName() const;
};
