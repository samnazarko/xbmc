/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AMLInsecureVideoCodec.h"

class amlogic::MJPEGCodec : public amlogic::AMLInsecureVideoCodec
{
private:
	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const;

	virtual int pre_header_feeding(am_private_t *para, am_packet_t *pkt) const;

public:
	MJPEGCodec(CProcessInfo &processInfo);
	virtual ~MJPEGCodec();

	virtual std::string getFormatName() const;

};
