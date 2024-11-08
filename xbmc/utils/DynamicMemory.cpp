/*
 *	(c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <string.h>

#include "DynamicMemory.h"

#include "utils/log.h"

static const unsigned rsvdSize = 0x4000;


DynamicMemory::DynamicMemory(unsigned size)
	: m_data(nullptr), m_realSize(0), m_currentSize(0)
{
	allocate(size);
}

DynamicMemory::~DynamicMemory()
{
	if (m_data) {
		delete[] m_data, m_data = nullptr;
	}
}

void DynamicMemory::allocate(unsigned newSize)
{
	if (newSize <= m_realSize) {
		return;
	}

	unsigned char *data = new unsigned char[newSize];

	if (m_currentSize && m_data) {
		memcpy(data, m_data, m_currentSize);
	}

	if (m_data) {
		delete[] m_data;
	}

	m_data = data;
	m_realSize = newSize;
}

void DynamicMemory::append(void *data, unsigned size)
{
	if (m_currentSize + size > m_realSize) {
		allocate(m_currentSize + size + rsvdSize);
	}

	memcpy(m_data + m_currentSize, data, size);
	m_currentSize += size;
}
