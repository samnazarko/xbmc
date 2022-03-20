/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AMLVideoCodec.h"

#include "threads/Atomics.h"

#include <chrono>
#include <thread>

using namespace amlogic;

std::atomic_flag AMLVideoCodec::m_pollSync = ATOMIC_FLAG_INIT;
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
	CAtomicSpinLock lock(m_pollSync);

	if (m_videoCodec == nullptr) {
		// no codec, no reason to poll
		return 0;
	}

	return m_videoCodec->poll();
}

/*
 * Just sleep a while until we think the next frame could be ready
 */
void AMLVideoCodec::sleep()
{
	if (m_videoCodec == nullptr) {
		return;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(m_videoCodec->getSleepDurationInMs()));
}
