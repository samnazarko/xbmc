/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <string>

#include "utils/log.h"
#include "DynamicDll.h"

#include "OSMCSecureOS.h"

OSMCSecureOS *OSMCSecureOS::m_instance = nullptr;
CCriticalSection OSMCSecureOS::m_mutex;

// ----------------------------------------------------------------------------

class LibsecureOSMCInterface
{
public:
	virtual ~LibsecureOSMCInterface() {};

	virtual void *osmc_init(void (*logf)(const char *, ...))=0;
	virtual void osmc_close(void *ctxt)=0;

	virtual int auth_get_key_id(void *ctx, char *id, uint32_t *id_length)=0;
	virtual int auth_get_totp(void *ctx, char *totp, uint32_t *totp_length)=0;
};

class LibsecureOSMC : public DllDynamic, LibsecureOSMCInterface
{
	DECLARE_DLL_WRAPPER(LibsecureOSMC, "libsecureOSMC.so")

	DEFINE_METHOD_FP(void *, osmc_init,  (void (*)(const char *, ...)))
	DEFINE_METHOD1(void,     osmc_close, (void *p1))

	DEFINE_METHOD3(int, auth_get_key_id, (void *p1, char *p2, uint32_t *p3))
	DEFINE_METHOD3(int, auth_get_totp,   (void *p1, char *p2, uint32_t *p3))

	BEGIN_METHOD_RESOLVE()
		RESOLVE_METHOD_FP(osmc_init)
		RESOLVE_METHOD(osmc_close)
		RESOLVE_METHOD(auth_get_key_id)
		RESOLVE_METHOD(auth_get_totp)
	END_METHOD_RESOLVE()

public:
	virtual void *osmc_init(void (*logf)(const char *, ...))
	{
		if (m_osmc_init) {
			return m_osmc_init(logf);
		}

		return nullptr;
	}

};

// ----------------------------------------------------------------------------

extern "C" void osmcsecureos_logf(const char *format, ...)
{
  va_list arglist;
  char buf[512];

  va_start(arglist, format);
  vsnprintf(buf, sizeof(buf), format, arglist);
  va_end(arglist);

  CLog::Log(LOGINFO, buf);
}

OSMCSecureOS::OSMCSecureOS()
	: m_osmcCtxt(nullptr)
{
	m_dll = new LibsecureOSMC();
	if (!m_dll->Load()) {
		CLog::Log(LOGWARNING, "OSMCSecureOS::OSMCSecureOS: unable to load libsecureOSMC");
		delete m_dll;
		m_dll = nullptr;
		return;
	}

	m_osmcCtxt = m_dll->osmc_init(osmcsecureos_logf);
	if (m_osmcCtxt) {
		CLog::Log(LOGWARNING, "OSMCSecureOS: key-id = {}", getKeyId());
	}
}

OSMCSecureOS::~OSMCSecureOS()
{
        std::unique_lock<CCriticalSection> lock(m_mutex);

	if (m_osmcCtxt) {
		m_dll->osmc_close(m_osmcCtxt);
	}

	if (m_dll) {
		delete m_dll, m_dll = nullptr;
	}

	m_instance = nullptr;
}

OSMCSecureOS &OSMCSecureOS::getInstance()
{
	std::unique_lock<CCriticalSection> lock(m_mutex);

	if (m_instance == nullptr) {
		m_instance = new OSMCSecureOS();
	}

	return *m_instance;
}

std::string OSMCSecureOS::getKeyId() const
{
	char id[65];
	uint32_t idLength = sizeof(id);

	if (m_dll->auth_get_key_id(m_osmcCtxt, id, &idLength)) {
		CLog::Log(LOGWARNING, "OSMCSecureOS::getKeyId: unable to get key id");
		return "<unknown>";
	}

	return std::string(id, idLength);
}

std::string OSMCSecureOS::getTOTP() const
{
	char totp[9];
	uint32_t totpLength = sizeof(totp);

	if (m_dll->auth_get_totp(m_osmcCtxt, totp, &totpLength)) {
		CLog::Log(LOGWARNING, "OSMCSecureOS::getTOTP: unable to get totp");
		return "<unknown>";
	}

	return std::string(totp, totpLength);
}
