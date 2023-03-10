/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>
#include <mutex>

#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "utils/Geometry.h"

#include "namespace.h"

class CDVDStreamInfo;
class CProcessInfo;
class VideoPicture;

class amlogic::AMLVideoCodec
{
private:
	static AMLVideoCodec	*m_videoCodec;

protected:
	CProcessInfo &m_processInfo;

	virtual int poll() = 0;
	virtual int getSleepDurationInMs() const = 0;

public:
	AMLVideoCodec(CProcessInfo &processInfo);
	virtual ~AMLVideoCodec();

	virtual bool openDecoder(CDVDStreamInfo &hints) = 0;
	virtual void closeDecoder() = 0;

	virtual std::string getFormatName() const = 0;

	virtual int getOMXPts() const = 0;
	virtual uint32_t getBufferIndex() const = 0;
	virtual int getAmlDuration() const = 0;

	virtual double getAspectRatio() const = 0;

	virtual bool prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts) = 0;
	virtual bool addData(uint8_t *pData, size_t iSize, double dts, double pts, uint8_t subtitlePlane) = 0;
	virtual CDVDVideoCodec::VCReturn getPicture(VideoPicture *pVideoPicture) = 0;
	virtual int releaseFrame(const uint32_t index, bool drop=false) = 0;

	virtual void setVideoRect(const CRect &SrcRect, const CRect &DestRect) = 0;

	virtual void setSpeed(int speed) = 0;
	virtual void setDrain(bool drain) = 0;

	static int pollFrame();
	static void sleep();

	virtual void reset() = 0;
};
