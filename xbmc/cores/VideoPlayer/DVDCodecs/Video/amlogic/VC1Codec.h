/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AMLInsecureVideoCodec.h"

#include <string>

class amlogic::VC1Codec : public amlogic::AMLInsecureVideoCodec
{
private:
	std::string	m_formatName;

	void wmv3_set_header(am_private_t *para, am_packet_t *pkt) const;
	void wvc1_set_header(am_private_t *para, am_packet_t *pkt) const;

protected:
	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const;

	virtual void getDeinterlacingMethods(std::list<EINTERLACEMETHOD> &methods) const;
	virtual void setupVideoCodecParams(aml_generic_param &params) const;

	virtual int pre_header_feeding(am_private_t *para, am_packet_t *pkt) const;
	virtual int set_header_info(am_private_t *para) const;
	virtual int write_header(am_private_t *para, am_packet_t *pkt) const;

public:
	VC1Codec(CProcessInfo &processInfo, std::string formatName);
	virtual ~VC1Codec();

	virtual std::string getFormatName() const;

	virtual bool prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts);

};
