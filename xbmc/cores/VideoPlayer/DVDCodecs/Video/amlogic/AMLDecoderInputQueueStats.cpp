/*
 *  Copyright (C) 2024 OSMC
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AMLDecoderInputQueueStats.h"

#include "LibAmcodec.h"

using namespace amlogic;

AMLDecoderInputQueueStats::AMLDecoderInputQueueStats(amlogic::LibAmcodec *lib)
	: m_libamcodec(lib), m_inputBufferLength(0)
{
}

AMLDecoderInputQueueStats::~AMLDecoderInputQueueStats()
{
}

void AMLDecoderInputQueueStats::addFrame(size_t frameLength)
{
	m_frameSizes.push_back(frameLength);
	m_inputBufferLength += frameLength;
}

unsigned AMLDecoderInputQueueStats::frameCount()
{
	struct buf_status bs;

	m_libamcodec->getVbufState(bs);

	while (m_inputBufferLength > (size_t) bs.data_len) {
		m_inputBufferLength -= m_frameSizes.front();
		m_frameSizes.pop_front();
	}

	return (unsigned) m_frameSizes.size();
}

bool AMLDecoderInputQueueStats::isAvailable(size_t frameLength) const
{
	struct buf_status bs;

	m_libamcodec->getVbufState(bs);

	return frameLength < bs.free_len;
}

float AMLDecoderInputQueueStats::fillLevel() const
{
	struct buf_status bs;
	m_libamcodec->getVbufState(bs);

	return (float) bs.data_len / (float) (bs.data_len + bs.free_len);
}

void AMLDecoderInputQueueStats::reset()
{
	m_inputBufferLength = 0;
	m_frameSizes.clear();
}
