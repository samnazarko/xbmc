/*
 *  Copyright (C) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVCodec.h"

#include "utils/log.h"
#include "utils/SysfsUtils.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"

#include "OSMCSecureOS.h"

using namespace amlogic;

DVCodec::DVCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints)
	: AMLInsecureVideoCodec(processInfo), m_player_supports_dv(false), m_dv_enabled(false)
{
	int support_info = 0;

	if (!m_secureOSMC->isVeroV()
			|| SysfsUtils::GetInt("/sys/class/amdolby_vision/support_info", support_info)
			|| (support_info & 0x7) == 0) {
		// no DV support
		return;
	}

	m_player_supports_dv = true;

	setupDolbyVision(hints);
}

DVCodec::~DVCodec()
{
}

bool DVCodec::isDisplaySupportsDolbyVision() const
{
	std::string dv_cap;

	if (SysfsUtils::GetString("/sys/devices/virtual/amhdmitx/amhdmitx0/dv_cap", dv_cap)) {
		return false;
	}

	return dv_cap.find("DV_RGB_444_8BIT") != std::string::npos;
}

void DVCodec::setupDolbyVision(const CDVDStreamInfo &hints)
{
	bool isProfile4 = hints.dovi.dv_profile == 4;
	bool isProfile8HLG = hints.dovi.dv_profile == 8
			&& hints.dovi.dv_bl_signal_compatibility_id == 4;
	bool isDVoutputDisabled =
		CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_HDR_PROCESS) != 2;
	int dvFlags = 5;

	bool enable_dv = hints.hdrType == StreamHdrType::HDR_TYPE_DOLBYVISION && isDolbyVisionSupported()
			&& !isProfile4 && !isProfile8HLG;

	if (SysfsUtils::GetIntDec("/sys/module/amdolby_vision/parameters/dolby_vision_flags", dvFlags))
		CLog::Log(LOGERROR, "DVCodec: Failed to get DV flags");

	if (!isDisplaySupportsDolbyVision() || isDVoutputDisabled) {
		CLog::Log(LOGDEBUG, "DVCodec: DV output to HDR/SDR");
		if (hints.dovi.dv_profile != 5)
			enable_dv = false;
		dvFlags |= 0x2000;
	} else
		dvFlags &= ~0x2000;

	if (SysfsUtils::SetInt("/sys/module/amdolby_vision/parameters/dolby_vision_flags", dvFlags))
		CLog::Log(LOGERROR, "DVCodec: Failed to set DV flags to 0x{:08x}", dvFlags);

	CLog::Log(LOGDEBUG, "DVCodec: Profile: {}, CCID: {}",
			  hints.dovi.dv_profile, hints.dovi.dv_bl_signal_compatibility_id);
	CLog::Log(LOGDEBUG, "DVCodec: stream type: {}, DV supported: {}, display supports DV: {}{}, DV enabled: {}",
			  hints.hdrType, isDolbyVisionSupported(), isDisplaySupportsDolbyVision(), isDVoutputDisabled ? " (disabled)" : "", enable_dv);

	if (SysfsUtils::SetString("/sys/module/amdolby_vision/parameters/dolby_vision_enable", enable_dv ? "Y" : "N")) {
		CLog::Log(LOGERROR, "DVCodec: unable to enable dv support");
		return;
	}

	if (!enable_dv) {
		return;
	}

	CLog::Log(LOGINFO, "DVCodec: DV support enabled");
	m_dv_enabled = true;
}

void DVCodec::setupVideoCodecParams(aml_generic_param &params) const
{
	AMLInsecureVideoCodec::setupVideoCodecParams(params);

	params.use_dv_vpath = m_dv_enabled;
}