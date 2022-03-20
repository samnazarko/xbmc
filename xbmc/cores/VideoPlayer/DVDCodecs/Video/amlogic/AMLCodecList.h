/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "threads/CriticalSection.h"

#include "namespace.h"

#include <vector>

class amlogic::AMLCodecList
{
private:
	static CCriticalSection	 m_mutex;
	static AMLCodecList		*m_instance;

	std::vector<AMLVideoCodecInfo*> m_codecInfo;

	AMLCodecList();
	~AMLCodecList();

public:
	static AMLCodecList &getInstance();

	void registerAmlVideoCodec(AMLVideoCodecInfo *info);

	unsigned getCodecCount() const;
	AMLVideoCodecInfo &getCodecInfoAt(unsigned index) const;
};
