/*
 *  Copyright (C) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVCodec.h"

#include "utils/log.h"
#include "utils/SysfsUtils.h"

#include "OSMCSecureOS.h"

using namespace amlogic;

DVCodec::DVCodec(CProcessInfo &processInfo)
	: AMLInsecureVideoCodec(processInfo), m_player_supports_dv(false)
{
	int support_info = 0;

	if (!m_secureOSMC->isVeroV()
			|| SysfsUtils::GetInt("/sys/class/amdolby_vision/support_info", support_info)
			|| (support_info & 0x7) == 0) {
		// no DV support
		return;
	}

	m_player_supports_dv = true;

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

	return dv_cap.find("The Rx don't support DolbyVision") == std::string::npos;
}

void DVCodec::setupVideoCodecParams(aml_generic_param &params) const
{
	AMLInsecureVideoCodec::setupVideoCodecParams(params);

	bool enable_dv = m_hints.hdrType == StreamHdrType::HDR_TYPE_DOLBYVISION && isDolbyVisionSupported() && isDisplaySupportsDolbyVision();

	CLog::Log(LOGDEBUG, "DVCodec: stream type: {}, DV supported: {}, display supports DV: {}, DV enabled: {}",
			  m_hints.hdrType, isDolbyVisionSupported(), isDisplaySupportsDolbyVision(), enable_dv);

	if (SysfsUtils::SetString("/sys/module/amdolby_vision/parameters/dolby_vision_enable", enable_dv ? "Y" : "N")) {
		CLog::Log(LOGERROR, "DVCodec: unable to enable dv support");
		return;
	}

	if (enable_dv) {
		// enable display-led DV
		if (SysfsUtils::SetString("/sys/module/amdolby_vision/parameters/dolby_vision_efuse_bypass", "Y")) {
			CLog::Log(LOGERROR, "DVCodec: unable to enable display-led support");
			return;
		}

		CLog::Log(LOGINFO, "DVCodec: DV support enabled");
	}

	params.use_dv_vpath = enable_dv;
}

#if 0


ll_enable=0 ... tv/display led
ll_enable=1 ... player led



bool aml_dv_support_ll()
{
  int support_ll = 0;
  CRegExp regexp;
  regexp.RegComp("YCbCr_422_12BIT");
  std::string valstr;
  CSysfsPath dv_cap{"/sys/devices/virtual/amhdmitx/amhdmitx0/dv_cap"};
  if (dv_cap.Exists())
  {
    valstr = dv_cap.Get<std::string>().value();
    support_ll = (regexp.RegFind(valstr) >= 0) ? 1 : 0;
  }

  return support_ll;
}




  // enable Dolby Vision driver when 'dovi.ko' is available
  bool device_support_dv(aml_support_dolby_vision());
  bool user_dv_disable(CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_COREELEC_AMLOGIC_DV_DISABLE));
  bool dv_enable(device_support_dv && !user_dv_disable &&
    hints.hdrType == StreamHdrType::HDR_TYPE_DOLBYVISION && (aml_display_support_dv() || hints.dovi.dv_profile == 5));
  CLog::Log(LOGDEBUG, "CAMLCodec::OpenDecoder {}DV support, {}, DV system is {}", device_support_dv ? "" : "no ",
    user_dv_disable ? "disabled" : "enabled", dv_enable ? "enabled" : "disabled");
  if (dv_enable)
  {
    // enable Dolby Vision
    CSysfsPath("/sys/module/amdolby_vision/parameters/dolby_vision_enable", 'Y');

    // force player led mode when enabled
    CSysfsPath dolby_vision_flags{"/sys/module/amdolby_vision/parameters/dolby_vision_flags"};
    CSysfsPath dolby_vision_ll_policy{"/sys/module/amdolby_vision/parameters/dolby_vision_ll_policy"};
    if (dolby_vision_flags.Exists() && dolby_vision_ll_policy.Exists())
    {
      if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_COREELEC_AMLOGIC_USE_PLAYERLED))
      {
        dolby_vision_flags.Set(dolby_vision_flags.Get<unsigned int>().value() | FLAG_FORCE_DV_LL);
        dolby_vision_ll_policy.Set(DOLBY_VISION_LL_YUV422);
      }
      else
      {
        dolby_vision_flags.Set(dolby_vision_flags.Get<unsigned int>().value() & ~(FLAG_FORCE_DV_LL));
        dolby_vision_ll_policy.Set(DOLBY_VISION_LL_DISABLE);
      }
    }

    am_private->gcodec.dv_enable = 1;
    if ((hints.dovi.dv_profile == 4 || hints.dovi.dv_profile == 7) && CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(
        CSettings::SETTING_VIDEOPLAYER_CONVERTDOVI) == 0)
    {
      if (dovi_el_type != ELType::TYPE_MEL) // use stream path if not MEL
      {
        CSysfsPath amdolby_vision_debug{"/sys/class/amdolby_vision/debug"};
        if (amdolby_vision_debug.Exists())
          amdolby_vision_debug.Set("enable_fel 1");
        am_private->gcodec.dec_mode  = STREAM_TYPE_STREAM;
      }
    }
  }
  else if (device_support_dv)
  {
    // disable Dolby Vision
    CSysfsPath("/sys/module/amdolby_vision/parameters/dolby_vision_enable", 'N');
  }




#endif
