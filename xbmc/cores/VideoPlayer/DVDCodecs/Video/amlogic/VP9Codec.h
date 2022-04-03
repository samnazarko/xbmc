/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AMLInsecureVideoCodec.h"

class amlogic::VP9Codec : public amlogic::AMLInsecureVideoCodec
{
protected:
	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const;

	virtual bool handleMasteringMetadata(const CDVDStreamInfo &hints) const;
	virtual int set_header_info(am_private_t *para) const;

	virtual void setupVideoCodecParams(aml_generic_param &params) const;

public:
	VP9Codec(CProcessInfo &processInfo);
	virtual ~VP9Codec();

	virtual std::string getFormatName() const;
};
