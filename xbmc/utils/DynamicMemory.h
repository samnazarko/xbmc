/*
 *	(c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

class DynamicMemory
{
private:
	unsigned char	*m_data;
	unsigned		 m_realSize;
	unsigned		 m_currentSize;

public:
	DynamicMemory(unsigned size = 10 * 1024);
	virtual ~DynamicMemory();

	inline bool isEmpty() const {
		return m_currentSize == 0 || m_data == nullptr;
	}

	inline void clear() {
		m_currentSize = 0;
	}

	inline unsigned size() const {
		return m_currentSize;
	}

	inline const unsigned char *data() const {
		return m_data;
	}

	void allocate(unsigned newSize);
	void append(void *data, unsigned size);
};
