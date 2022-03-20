/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AMLCodecList.h"

#include "AMLVideoCodecInfo.h"

#include "threads/SingleLock.h"
#include "threads/CriticalSection.h"


using namespace amlogic;

CCriticalSection AMLCodecList::m_mutex;
AMLCodecList *AMLCodecList::m_instance = nullptr;

AMLCodecList::AMLCodecList()
{
}

AMLCodecList::~AMLCodecList()
{
	CSingleLock lock(m_mutex);

	m_instance = nullptr;
}

AMLCodecList &AMLCodecList::getInstance()
{
	CSingleLock lock(m_mutex);

	if (m_instance == nullptr) {
		m_instance = new AMLCodecList();
	}

	return *m_instance;
}

void AMLCodecList::registerAmlVideoCodec(AMLVideoCodecInfo *info)
{
	CSingleLock lock(m_mutex);

	m_codecInfo.push_back(info);
}

unsigned AMLCodecList::getCodecCount() const
{
	return m_codecInfo.size();
}

AMLVideoCodecInfo &AMLCodecList::getCodecInfoAt(unsigned index) const
{
	return *m_codecInfo.at(index);
}
