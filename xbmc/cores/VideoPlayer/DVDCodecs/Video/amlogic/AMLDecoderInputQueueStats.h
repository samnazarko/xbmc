/*
 *  Copyright (C) 2024 OSMC
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <sys/types.h>

#include <deque>

#include "namespace.h"

class amlogic::AMLDecoderInputQueueStats
{
private:
	amlogic::LibAmcodec	*m_libamcodec;
	size_t				 m_inputBufferLength;
	std::deque<size_t>	 m_frameSizes;

public:
	AMLDecoderInputQueueStats(amlogic::LibAmcodec *lib);
	virtual ~AMLDecoderInputQueueStats();

	bool isAvailable(size_t frameLength) const;
	float fillLevel() const;

	void addFrame(size_t frameLength);
	unsigned frameCount();
	void reset();
};
