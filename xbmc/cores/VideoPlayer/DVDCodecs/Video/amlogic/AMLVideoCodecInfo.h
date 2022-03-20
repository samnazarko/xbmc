/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "namespace.h"

#include <string>

extern "C" {
	#include <libavcodec/codec_id.h>
}

class CDVDStreamInfo;
class CProcessInfo;

class amlogic::AMLVideoCodecInfo
{
private:

public:
	virtual ~AMLVideoCodecInfo() = default;

	virtual std::string getCodecName() const = 0;
	virtual bool isSecure() const = 0;
	virtual bool canPlay(const CDVDStreamInfo &hints) const = 0;
	virtual AMLVideoCodec *createCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints) = 0;
};
