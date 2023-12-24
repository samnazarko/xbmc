/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AMLVideoCodec.h"

#include <chrono>
#include <thread>

using namespace amlogic;

static std::mutex m_pollSync;
AMLVideoCodec *AMLVideoCodec::m_videoCodec;

AMLVideoCodec::AMLVideoCodec(CProcessInfo &processInfo)
	: m_processInfo(processInfo)
{
	m_videoCodec = this;
}

AMLVideoCodec::~AMLVideoCodec()
{
	m_videoCodec = nullptr;
}

int AMLVideoCodec::pollFrame()
{
	std::lock_guard<std::mutex> lock(m_pollSync);

	if (m_videoCodec == nullptr) {
		// no codec, no reason to poll
		return 0;
	}

	return m_videoCodec->poll();
}
