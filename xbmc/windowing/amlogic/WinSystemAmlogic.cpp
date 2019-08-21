/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemAmlogic.h"

#include <string.h>
#include <float.h>

#include "ServiceBroker.h"
#include "cores/RetroPlayer/process/amlogic/RPProcessInfoAmlogic.h"
#include "cores/RetroPlayer/rendering/VideoRenderers/RPRendererOpenGLES.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecAmlogic.h"
#include "cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.h"
#include "cores/VideoPlayer/VideoRenderers/HwDecRender/RendererAML.h"
#include "cores/AudioEngine/AESinkFactory.h"
#include "cores/AudioEngine/Sinks/AESinkALSA.h"
#include "windowing/GraphicContext.h"
#include "windowing/Resolution.h"
#include "platform/linux/powermanagement/LinuxPowerSyscall.h"
#include "platform/linux/ScreenshotSurfaceAML.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "guilib/DispResource.h"
#include "utils/AMLUtils.h"
#include "utils/log.h"
#include "utils/SysfsUtils.h"
#include "utils/StringUtils.h"
#include "threads/SingleLock.h"
#include "messaging/ApplicationMessenger.h"
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>

using namespace KODI::MESSAGING;

#include <linux/fb.h>

#include "system_egl.h"

using namespace KODI;

CWinSystemAmlogic::CWinSystemAmlogic() :
  m_libinput(new CLibInputHandler)
{
  const char *env_framebuffer = getenv("FRAMEBUFFER");

  // default to framebuffer 0
  m_framebuffer_name = "fb0";
  if (env_framebuffer)
  {
    std::string framebuffer(env_framebuffer);
    std::string::size_type start = framebuffer.find("fb");
    m_framebuffer_name = framebuffer.substr(start);
  }

  m_nativeDisplay = EGL_NO_DISPLAY;
  m_nativeWindow = static_cast<EGLNativeWindowType>(NULL);

  m_displayWidth = 0;
  m_displayHeight = 0;

  m_stereo_mode = RENDER_STEREO_MODE_OFF;
  m_delayDispReset = false;

  aml_permissions();
  aml_disable_freeScale();

 /* Take in to account custom OSMC parameters */
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOSCREEN_FORCERGB)) {
    CLog::Log(LOGDEBUG, "CEGLNativeTypeAmlogic::Initialize -- forcing RGB");
    SysfsUtils::SetString("/sys/class/amhdmitx/amhdmitx0/output_rgb", "1");
 }

  int range_control;
  SysfsUtils::GetInt("/sys/module/am_vecm/parameters/range_control", range_control);
  if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOSCREEN_LIMITEDRANGEAML))
    range_control &= 1;
  else
    range_control |= 2;
  CLog::Log(LOGDEBUG, "CEGLNativeTypeAmlogic::Initialize -- setting quantization range to {}",
      range_control & 2 ? "full" : "limited");
  SysfsUtils::SetInt("/sys/module/am_vecm/parameters/range_control", range_control);

 if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOSCREEN_LOCKHPD)) {
    CLog::Log(LOGDEBUG, "CEGLNativeTypeAmlogic::Initialize -- forcing HPD to be locked");
    SysfsUtils::SetString("/sys/class/amhdmitx/amhdmitx0/debug", "hpd_lock1");
 }

 std::string attr = "";
 SysfsUtils::GetString("/sys/class/amhdmitx/amhdmitx0/attr", attr);

 if (CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOSCREEN_FORCE422)) {
   if (attr.find("444") != std::string::npos ||
       attr.find("422") != std::string::npos ||
       attr.find("420") != std::string::npos)
     attr.replace(attr.find("4"),3,"422").append("now");
   else
     attr.append("422now");
 }
 else {
   if (attr.find("422") != std::string::npos)
     attr.erase(attr.find("4"),3);
   attr.append("now");
 }
 CLog::Log(LOGDEBUG, "CEGLNativeTypeAmlogic::Initialize -- setting 422 output, attr = {}", attr);
 SysfsUtils::SetString("/sys/class/amhdmitx/amhdmitx0/attr", attr.c_str());

 int maxlum = 100;
 maxlum = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOSCREEN_MAXLUM);
 CLog::Log(LOGDEBUG, "CEGLNativeTypeAmlogic::Initialize -- setting max lum to {}", maxlum);
 SysfsUtils::SetInt("/sys/module/am_vecm/parameters/customer_panel_lumin", maxlum);

 SysfsUtils::GetString("/sys/class/amhdmitx/amhdmitx0/rawedid", m_lastEdid);
 StartMonitorHWEvent();

  // Register sink
  AE::CAESinkFactory::ClearSinks();
  CAESinkALSA::Register();
  m_libinput->Start();
}

CWinSystemAmlogic::~CWinSystemAmlogic()
{
  StopMonitorHWEvent();

  if(m_nativeWindow)
  {
    m_nativeWindow = static_cast<EGLNativeWindowType>(NULL);
  }
}

void hwMon(CWinSystemAmlogic *instance) {

        struct udev *udev;
        struct udev_device *dev;

        struct udev_monitor *mon;
        int fd;

        /* Create the udev object */
        udev = udev_new();
        if (!udev) {
                return;
        }

        mon = udev_monitor_new_from_netlink(udev, "udev");
        udev_monitor_filter_add_match_subsystem_devtype(mon, "extcon", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(mon, "switch", NULL);

        udev_monitor_enable_receiving(mon);
        fd = udev_monitor_get_fd(mon);

        /* Poll for events */

        while (1 && instance->m_monitorEvents) {

                fd_set fds;
                struct timeval tv;
                int ret;

                FD_ZERO(&fds);
                FD_SET(fd, &fds);
                tv.tv_sec = 0;
                tv.tv_usec = 0;

                ret = select(fd+1, &fds, NULL, NULL, &tv);

                /* Check if FD has received data */

                if (ret > 0 && FD_ISSET(fd, &fds)) {

                        dev = udev_monitor_receive_device(mon);
                        if (dev) {
                                CLog::Log(LOGDEBUG, "CEGLNativeTypeAmlogic: Detected HDMI switch");
                                int state;
                                SysfsUtils::GetInt("/sys/class/amhdmitx/amhdmitx0/hpd_state", state);
				std::string newEdid;
				SysfsUtils::GetString("/sys/class/amhdmitx/amhdmitx0/rawedid", newEdid);

                                if (state && newEdid != instance->m_lastEdid) {
                                    CServiceBroker::GetAppMessenger()->PostMsg(TMSG_AML_RESIZE);
				    instance->m_lastEdid = newEdid;
				}
                                udev_device_unref(dev);
                        }
                        else {
                                CLog::Log(LOGERROR, "CEGLNativeTypeAmlogic: can't get device from receive_device");
                        }
                }
                usleep(250*1000);
        }
}

void CWinSystemAmlogic::StartMonitorHWEvent() {
    CLog::Log(LOGDEBUG, "CEGLNativeTypeAmlogic::StartMonitorHWEvent -- starting event monitor for HDMI hotplug events");
    m_monitorEvents = true;
    m_monitorThread = std::thread(hwMon, this);
    return;
}

void CWinSystemAmlogic::StopMonitorHWEvent() {
    CLog::Log(LOGDEBUG, "CEGLNativeTypeAmlogic::StopMonitorHWEvent -- stopping event monitor for HDMI hotplug events");
    m_monitorEvents = false;
    m_monitorThread.join();
    return;
}

bool CWinSystemAmlogic::InitWindowSystem()
{
  m_nativeDisplay = EGL_DEFAULT_DISPLAY;

  CDVDVideoCodecAmlogic::Register();
  CLinuxRendererGLES::Register();
  RETRO::CRPProcessInfoAmlogic::Register();
  RETRO::CRPProcessInfoAmlogic::RegisterRendererFactory(new RETRO::CRendererFactoryOpenGLES);
  CRendererAML::Register();
  CScreenshotSurfaceAML::Register();

  aml_set_framebuffer_resolution(1920, 2205, m_framebuffer_name);

  return CWinSystemBase::InitWindowSystem();
}

bool CWinSystemAmlogic::DestroyWindowSystem()
{
  return true;
}

const RESOLUTION_INFO &CWinSystemAmlogic::Choose3dRes(RENDER_STEREO_MODE stereo_mode, const RESOLUTION_INFO &res)
{
  if (stereo_mode != RENDER_STEREO_MODE_SPLIT_HORIZONTAL
      && stereo_mode != RENDER_STEREO_MODE_SPLIT_VERTICAL
      && stereo_mode != RENDER_STEREO_MODE_HARDWAREBASED)
  {
    // unsupported (3D) mode or no 3D mode at all
    return res;
  }

  uint32_t flags3d = stereo_mode == RENDER_STEREO_MODE_SPLIT_VERTICAL ? D3DPRESENTFLAG_MODE3DSBS : D3DPRESENTFLAG_MODE3DTB;
  flags3d |= (res.dwFlags&D3DPRESENTFLAG_INTERLACED);

  CLog::Log(LOGDEBUG, "Search for exact 3D mode match for mode {}", res.strMode);

  for (size_t i = 0; i < resolutions3d.size(); i++)
  {
    if ((stereo_mode == RENDER_STEREO_MODE_HARDWAREBASED && resolutions3d[i].iBlanking == 0) ||
        (stereo_mode != RENDER_STEREO_MODE_HARDWAREBASED && resolutions3d[i].iBlanking != 0))
      continue;

    if ((resolutions3d[i].dwFlags&D3DPRESENTFLAG_MODEMASK) == flags3d
        && resolutions3d[i].iScreenWidth == res.iScreenWidth
        && resolutions3d[i].iScreenHeight == res.iScreenHeight
        && resolutions3d[i].fRefreshRate == res.fRefreshRate)
    {
      CLog::Log(LOGDEBUG, "exact match found: {}", resolutions3d[i].strMode);
      return resolutions3d[i];
    }
  }

  CLog::Log(LOGDEBUG, "Search for 3D mode matching resolution");

  for (size_t i = 0; i < resolutions3d.size(); i++)
  {
	if ((stereo_mode == RENDER_STEREO_MODE_HARDWAREBASED && resolutions3d[i].iBlanking == 0) ||
        (stereo_mode != RENDER_STEREO_MODE_HARDWAREBASED && resolutions3d[i].iBlanking != 0))
      continue;

    if ((resolutions3d[i].dwFlags&D3DPRESENTFLAG_MODEMASK) == flags3d
        && resolutions3d[i].iScreenWidth == res.iScreenWidth
        && resolutions3d[i].iScreenHeight == res.iScreenHeight)
    {
      CLog::Log(LOGDEBUG, "matching resolution found: {}", resolutions3d[i].strMode);
      return resolutions3d[i];
    }
  }

   CLog::Log(LOGDEBUG, "No matching 3D mode found, using {}", res.strMode);

  return res;
}

bool CWinSystemAmlogic::CreateNewWindow(const std::string& name,
                                    bool fullScreen,
                                    RESOLUTION_INFO& res)
{
  RESOLUTION_INFO current_resolution;
  current_resolution.iWidth = current_resolution.iHeight = 0;
  RENDER_STEREO_MODE stereo_mode = CServiceBroker::GetWinSystem()->GetGfxContext().GetStereoMode();

  m_nWidth        = res.iWidth;
  m_nHeight       = res.iHeight;
  m_displayWidth  = res.iScreenWidth;
  m_displayHeight = res.iScreenHeight;
  m_fRefreshRate  = res.fRefreshRate;

  if ((m_bWindowCreated && aml_get_native_resolution(&current_resolution)) &&
    current_resolution.iWidth == res.iWidth && current_resolution.iHeight == res.iHeight &&
    current_resolution.iScreenWidth == res.iScreenWidth && current_resolution.iScreenHeight == res.iScreenHeight &&
    m_bFullScreen == fullScreen && current_resolution.fRefreshRate == res.fRefreshRate &&
    (current_resolution.dwFlags & D3DPRESENTFLAG_MODEMASK) == (res.dwFlags & D3DPRESENTFLAG_MODEMASK) &&
    m_stereo_mode == stereo_mode)
  {
    CLog::Log(LOGDEBUG, "CWinSystemEGL::CreateNewWindow: No need to create a new window");
    return true;
  }

  int delay = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt("videoscreen.delayrefreshchange");
  if (delay > 0)
  {
    m_delayDispReset = true;
    m_dispResetTimer.Set(std::chrono::milliseconds(static_cast<unsigned int>(delay * 100)));
  }

  {
    std::unique_lock<CCriticalSection> lock(m_resourceSection);
    for (std::vector<IDispResource *>::iterator i = m_resources.begin(); i != m_resources.end(); ++i)
    {
      (*i)->OnLostDisplay();
    }
  }

  m_stereo_mode = stereo_mode;
  m_bFullScreen = fullScreen;

#ifdef _FBDEV_WINDOW_H_
  fbdev_window *nativeWindow = new fbdev_window;
  nativeWindow->width = res.iWidth;
  nativeWindow->height = res.iHeight;
  m_nativeWindow = static_cast<EGLNativeWindowType>(nativeWindow);
#endif

  aml_set_native_resolution(res, m_framebuffer_name, stereo_mode);

  if (!m_delayDispReset)
  {
    std::unique_lock<CCriticalSection> lock(m_resourceSection);
    // tell any shared resources
    for (std::vector<IDispResource *>::iterator i = m_resources.begin(); i != m_resources.end(); ++i)
    {
      (*i)->OnResetDisplay();
    }
  }

  return true;
}

bool CWinSystemAmlogic::DestroyWindow()
{
  m_nativeWindow = static_cast<EGLNativeWindowType>(NULL);

  return true;
}

static std::string ModeFlagsToString(unsigned int flags, bool identifier)
{
  std::string res;
  if(flags & D3DPRESENTFLAG_INTERLACED)
    res += "i";
  else
    res += "p";

  if(!identifier)
    res += " ";

  if(flags & D3DPRESENTFLAG_MODE3DSBS)
    res += "sbs";
  else if(flags & D3DPRESENTFLAG_MODE3DTB)
    res += "tab";
  else if(identifier)
    res += "std";
  return res;
}

void CWinSystemAmlogic::Update3dResolutions()
{
  if (!aml_probe_3d_resolutions(resolutions3d) || resolutions3d.empty())
  {
    CLog::Log(LOGWARNING, "{}: No valid 3D resolutions found.",__FUNCTION__);
    return;
  }

  for (size_t i = 0; i < resolutions3d.size(); i++)
  {
    std::string m3d = "<unknown>";
    if (resolutions3d[i].dwFlags & D3DPRESENTFLAG_MODE3DSBS)
      m3d = "SBS";
    else if (resolutions3d[i].dwFlags & D3DPRESENTFLAG_MODE3DTB)
      m3d = resolutions3d[i].iBlanking ? "FP" : "TAB";

    CLog::Log(LOGINFO, "Found 3D resolution {} x {} with {} x {}{} @ {:f} ({})\n",
      resolutions3d[i].iWidth,
      resolutions3d[i].iHeight,
      resolutions3d[i].iScreenWidth,
      resolutions3d[i].iScreenHeight,
      resolutions3d[i].dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "",
      resolutions3d[i].fRefreshRate,
      m3d);
  }
}

void CWinSystemAmlogic::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  RESOLUTION_INFO resDesktop, curDisplay;
  std::string curDesktopSetting, curResolution, newRes;
  std::vector<RESOLUTION_INFO> resolutions;

  if (!aml_probe_resolutions(resolutions) || resolutions.empty())
  {
    CLog::Log(LOGWARNING, "{}: ProbeResolutions failed.",__FUNCTION__);
  }

  /* ProbeResolutions includes already all resolutions.
   * Only get desktop resolution so we can replace xbmc's desktop res
   */
  if (aml_get_native_resolution(&curDisplay))
  {
    resDesktop = curDisplay;
  }

  curDesktopSetting = CServiceBroker::GetSettingsComponent()->GetSettings()->GetString(CSettings::SETTING_VIDEOSCREEN_SCREENMODE);

  curResolution = StringUtils::Format("{:05}{:05}{:09.5f}{}",
      resDesktop.iScreenWidth, resDesktop.iScreenHeight, resDesktop.fRefreshRate,
      ModeFlagsToString(resDesktop.dwFlags, true));

  if (curDesktopSetting == "DESKTOP")
    curDesktopSetting = curResolution;
  else if (curDesktopSetting.length() == 24)
    curDesktopSetting = StringUtils::Right(curDesktopSetting, 23);

  CLog::Log(LOGINFO, "Current display setting is {}", curDesktopSetting);
  CLog::Log(LOGINFO, "Current output resolution is {}", curResolution);

  RESOLUTION ResDesktop = RES_INVALID;
  RESOLUTION res_index  = RES_DESKTOP;
  bool resExactMatch = false;
  std::string ResString;
  std::string ResFallback = "00480024.00000istd";

  for (size_t i = 0; i < resolutions.size(); i++)
  {
    // if this is a new setting,
    // create a new empty setting to fill in.
    if ((int)CDisplaySettings::GetInstance().ResolutionInfoSize() <= res_index)
    {
      RESOLUTION_INFO res;
      CDisplaySettings::GetInstance().AddResolutionInfo(res);
    }

    CServiceBroker::GetWinSystem()->GetGfxContext().ResetOverscan(resolutions[i]);
    CDisplaySettings::GetInstance().GetResolutionInfo(res_index) = resolutions[i];

    CLog::Log(LOGINFO, "Found resolution {:d} x {:d} with {:d} x {:d}{} @ {:f} Hz\n",
      resolutions[i].iWidth,
      resolutions[i].iHeight,
      resolutions[i].iScreenWidth,
      resolutions[i].iScreenHeight,
      resolutions[i].dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "",
      resolutions[i].fRefreshRate);

    ResString = StringUtils::Format("{:05}{:05}{:09.5f}{}",
          resolutions[i].iScreenWidth, resolutions[i].iScreenHeight, resolutions[i].fRefreshRate,
          ModeFlagsToString(resolutions[i].dwFlags, true));
    if (curDesktopSetting == ResString){
      ResDesktop = res_index;
      resExactMatch = true;
      newRes = ResString;
      CLog::Log(LOGINFO, "Current resolution setting found at 16 + {}", i);
    }

    /* fall back to the highest resolution available but not more than current desktop */
    if(curDesktopSetting.substr(5,18).compare(ResString.substr(5,18)) >= 0 &&
        ResString.substr(5,18).compare(ResFallback) > 0 && ! resExactMatch)
    {
      ResDesktop = res_index;
      ResFallback = ResString.substr(5,18);
      newRes = ResString;
      CLog::Log(LOGINFO, "Fallback resolution at 16 + {} {}", i, ResFallback);
    }

    res_index = (RESOLUTION)((int)res_index + 1);
  }

  // set RES_DESKTOP
  if (ResDesktop != RES_INVALID)
  {
    CLog::Log(LOGINFO, "Found ({:d}x{:d}{}@{:f}) at {:d}, setting to RES_DESKTOP at {:d}",
      resDesktop.iWidth, resDesktop.iHeight,
      resDesktop.dwFlags & D3DPRESENTFLAG_INTERLACED ? "i" : "",
      resDesktop.fRefreshRate,
      (int)ResDesktop, (int)RES_DESKTOP);

    CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP) = CDisplaySettings::GetInstance().GetResolutionInfo(ResDesktop);
  }
  Update3dResolutions();
}

bool CWinSystemAmlogic::Hide()
{
  return false;
}

bool CWinSystemAmlogic::Show(bool show)
{
  std::string blank_framebuffer = "/sys/class/graphics/" + m_framebuffer_name + "/blank";
  SysfsUtils::SetInt(blank_framebuffer.c_str(), show ? 0 : 1);
  return true;
}

void CWinSystemAmlogic::Register(IDispResource *resource)
{
  std::unique_lock<CCriticalSection> lock(m_resourceSection);
  m_resources.push_back(resource);
}

void CWinSystemAmlogic::Unregister(IDispResource *resource)
{
  std::unique_lock<CCriticalSection> lock(m_resourceSection);
  std::vector<IDispResource*>::iterator i = find(m_resources.begin(), m_resources.end(), resource);
  if (i != m_resources.end())
    m_resources.erase(i);
}
