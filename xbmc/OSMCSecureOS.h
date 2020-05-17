/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "threads/CriticalSection.h"
#include <stdarg.h>

class LibsecureOSMC;

class OSMCSecureOS
{
private:
	static OSMCSecureOS		*m_instance;
	static CCriticalSection m_mutex;

	LibsecureOSMC	*m_dll;
	void			*m_osmcCtxt;

	OSMCSecureOS();
	~OSMCSecureOS();

public:
	static OSMCSecureOS &getInstance();

	std::string getKeyId() const;
	std::string getTOTP() const;

	int getMvcSubtitleDepth(double pts) const;

	void parseFrame(uint32_t codecTag, uint8_t subtitlePlane, int64_t pts, const uint8_t *data, size_t size) const;
};
