/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include <math.h>

#include "DVDCodecs/DVDFactoryCodec.h"
#include "DVDVideoCodecAmlogic.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "DVDStreamInfo.h"
#include "ServiceBroker.h"
#include "utils/AMLUtils.h"
#include "utils/BitstreamConverter.h"
#include "utils/log.h"
#include "utils/SysfsUtils.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "threads/Thread.h"

#include "amlogic/AMLCodecList.h"
#include "amlogic/AMLVideoCodecInfo.h"

#define __MODULE_NAME__ "DVDVideoCodecAmlogic"

CAMLVideoBufferPool::~CAMLVideoBufferPool()
{
  CLog::Log(LOGDEBUG, "CAMLVideoBufferPool::~CAMLVideoBufferPool: Deleting {:d} buffers", static_cast<unsigned int>(m_videoBuffers.size()) );
  for (auto buffer : m_videoBuffers)
    delete buffer;
}

CVideoBuffer* CAMLVideoBufferPool::Get()
{
  std::unique_lock<CCriticalSection> lock(m_criticalSection);

  if (m_freeBuffers.empty())
  {
    m_freeBuffers.push_back(m_videoBuffers.size());
    m_videoBuffers.push_back(new CAMLVideoBuffer(static_cast<int>(m_videoBuffers.size())));
  }
  int bufferIdx(m_freeBuffers.back());
  m_freeBuffers.pop_back();

  m_videoBuffers[bufferIdx]->Acquire(shared_from_this());

  return m_videoBuffers[bufferIdx];
}

void CAMLVideoBufferPool::Return(int id)
{
  std::unique_lock<CCriticalSection> lock(m_criticalSection);
  if (m_videoBuffers[id]->m_amlCodec)
  {
    m_videoBuffers[id]->m_amlCodec->releaseFrame(m_videoBuffers[id]->m_bufferIndex, true);
    m_videoBuffers[id]->m_amlCodec = nullptr;
  }
  m_freeBuffers.push_back(id);
}

/***************************************************************************/

CDVDVideoCodecAmlogic::CDVDVideoCodecAmlogic(CProcessInfo &processInfo)
  : CDVDVideoCodec(processInfo)
  , m_pFormatName("amcodec")
  , m_opened(false)
  , m_codecControlFlags(0)
{
}

CDVDVideoCodecAmlogic::~CDVDVideoCodecAmlogic()
{
  Dispose();
}

std::unique_ptr<CDVDVideoCodec> CDVDVideoCodecAmlogic::Create(CProcessInfo& processInfo)
{
  return std::make_unique<CDVDVideoCodecAmlogic>(processInfo);
}

bool CDVDVideoCodecAmlogic::Register()
{
  CDVDFactoryCodec::RegisterHWVideoCodec("amlogic_dec", CDVDVideoCodecAmlogic::Create);
  return true;
}

std::atomic<bool> CDVDVideoCodecAmlogic::m_InstanceGuard(false);

bool CDVDVideoCodecAmlogic::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  if (!CServiceBroker::GetSettingsComponent()->GetSettings()->GetBool(CSettings::SETTING_VIDEOPLAYER_USEAMCODEC))
    return false;
  if (hints.stills || hints.width == 0)
    return false;

  if (!aml_permissions())
  {
    CLog::Log(LOGERROR, "AML: no proper permission, please contact the device vendor. Skipping codec...");
    return false;
  }

  // allow only 1 instance here
  if (m_InstanceGuard.exchange(true))
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecAmlogic::Open - InstanceGuard locked");
    return false;
  }

  m_opened = false;

  m_hints = hints;

  CLog::Log(LOGDEBUG, "CDVDVideoCodecAmlogic::Opening: codec {:d} profile:{:d} extra_size:{:d}", m_hints.codec, hints.profile, hints.extradata.GetSize());

  bool needSecureDecoder = false;
#if notyet
  if (m_hints.cryptoSession) {
	  // TODO implement DRM playback
	  needSecureDecoder = true;
  }
#endif

  amlogic::AMLCodecList &codecList = amlogic::AMLCodecList::getInstance();

  for (unsigned cc = 0; cc < codecList.getCodecCount(); cc++) {
	  amlogic::AMLVideoCodecInfo &codecInfo = codecList.getCodecInfoAt(cc);

	  if (codecInfo.isSecure() != needSecureDecoder) {
		  CLog::Log(LOGDEBUG, "CDVDVideoCodecAmlogic: skipping decoder {} because security precondition not met (decoder is marked as {}secure)",
				  codecInfo.getCodecName(), codecInfo.isSecure() ? "" : "not ");
		  continue;
	  }

	  if (!codecInfo.canPlay(m_hints)) {
		  CLog::Log(LOGDEBUG, "CDVDVideoCodecAmlogic: skipping decoder {}", codecInfo.getCodecName());
		  continue;
	  }

	  CLog::Log(LOGINFO, "CDVDVideoCodecAmlogic: using Amlogic decoder {}", codecInfo.getCodecName());
	  m_codec = std::shared_ptr<amlogic::AMLVideoCodec>(codecInfo.createCodec(m_processInfo, m_hints));
	  break;
  }

  if (m_codec == nullptr) {
    // no suitable codec found
    CLog::Log(LOGERROR, "{}: Failed to create Amlogic Codec for codec id = {}", __MODULE_NAME__, m_hints.codec);
    goto FAIL;
  }

  m_pFormatName = m_codec->getFormatName().c_str();

  m_aspect_ratio = m_hints.aspect;

  // allocate a dummy VideoPicture buffer.
  m_videobuffer.Reset();

  m_videobuffer.iWidth  = m_hints.width;
  m_videobuffer.iHeight = m_hints.height;

  m_videobuffer.iDisplayWidth  = m_videobuffer.iWidth;
  m_videobuffer.iDisplayHeight = m_videobuffer.iHeight;
  if (m_hints.aspect > 0.0 && !m_hints.forced_aspect)
  {
    m_videobuffer.iDisplayWidth  = ((int)lrint(m_videobuffer.iHeight * m_hints.aspect)) & ~3;
    if (m_videobuffer.iDisplayWidth > m_videobuffer.iWidth)
    {
      m_videobuffer.iDisplayWidth  = m_videobuffer.iWidth;
      m_videobuffer.iDisplayHeight = ((int)lrint(m_videobuffer.iWidth / m_hints.aspect)) & ~3;
    }
  }

  m_processInfo.SetVideoDecoderName(m_pFormatName, true);
  m_processInfo.SetVideoDimensions(m_hints.width, m_hints.height);
  m_processInfo.SetVideoDeintMethod("hardware");
  m_processInfo.SetVideoDAR(m_hints.aspect);
  m_processInfo.SetVideoStereoMode(m_hints.stereo_mode);

  CLog::Log(LOGINFO, "{}: Opened Amlogic Codec", __MODULE_NAME__);
  return true;
FAIL:
  Dispose();
  return false;
}

void CDVDVideoCodecAmlogic::Dispose(void)
{
  m_videoBufferPool = nullptr;

  if (m_codec) {
    m_codec->closeDecoder(), m_codec = nullptr;
  }

  m_videobuffer.iFlags = 0;

  m_opened = false;
  m_InstanceGuard.exchange(false);
}

bool CDVDVideoCodecAmlogic::AddData(const DemuxPacket &packet)
{
  // Handle Input, add demuxer packet to input queue, we must accept it or
  // it will be discarded as VideoPlayerVideo has no concept of "try again".

  if (packet.iSize == 0 || packet.pData == nullptr) {
	  // we ignore empty packets
	  return true;
  }

  uint8_t *pData(packet.pData);
  size_t iSize(packet.iSize);

  if (!m_codec->prepareFrame(m_hints, pData, iSize, packet.dts, packet.pts)) {
	  // something went wrong or we haven't got enough data yet
      return true;
  }

  if (!m_opened) {
    if (packet.pts == DVD_NOPTS_VALUE) {
      m_hints.ptsinvalid = true;
    }

    if (m_codec && !m_codec->openDecoder(m_hints)) {
      CLog::Log(LOGERROR, "{}: Failed to open Amlogic Codec", __MODULE_NAME__);
    }

    m_videoBufferPool = std::shared_ptr<CAMLVideoBufferPool>(new CAMLVideoBufferPool());

    m_opened = true;
  }

  return m_codec->addData(pData, iSize, packet.dts, m_hints.ptsinvalid ? DVD_NOPTS_VALUE : packet.pts, packet.subtitlePlane);
}

void CDVDVideoCodecAmlogic::Reset(void)
{
  m_codec->reset();
}

CDVDVideoCodec::VCReturn CDVDVideoCodecAmlogic::GetPicture(VideoPicture* pVideoPicture)
{
  if (!m_codec) {
    return VC_ERROR;
  }

  VCReturn retVal = m_codec->getPicture(&m_videobuffer);

  if (retVal == VC_PICTURE)
  {
    pVideoPicture->SetParams(m_videobuffer);

    pVideoPicture->videoBuffer = m_videoBufferPool->Get();
    static_cast<CAMLVideoBuffer*>(pVideoPicture->videoBuffer)->Set(this, m_codec,
     m_codec->getOMXPts(), m_codec->getAmlDuration(), m_codec->getBufferIndex());;
  }

  m_aspect_ratio = m_codec->getAspectRatio();

  pVideoPicture->iDisplayWidth  = pVideoPicture->iWidth;
  pVideoPicture->iDisplayHeight = pVideoPicture->iHeight;

  //set default if there is no other information for TV/DVD modes
  if (pVideoPicture->iWidth > 0 && pVideoPicture->iWidth != 640 &&
      (pVideoPicture->iHeight == 480 || pVideoPicture->iHeight == 576) &&
      m_aspect_ratio < 0.1f)
      m_aspect_ratio = 16.0f / 9.0f;

  if (m_aspect_ratio > 1.0 && !m_hints.forced_aspect)
  {
    pVideoPicture->iDisplayWidth  = ((int)lrint(pVideoPicture->iHeight * m_aspect_ratio)) & ~3;
    if (pVideoPicture->iDisplayWidth > pVideoPicture->iWidth)
    {
      pVideoPicture->iDisplayWidth  = pVideoPicture->iWidth;
      pVideoPicture->iDisplayHeight = ((int)lrint(pVideoPicture->iWidth / m_aspect_ratio)) & ~3;
    }
  }

  return retVal;
}

void CDVDVideoCodecAmlogic::SetCodecControl(int flags)
{
  if (m_codecControlFlags != flags)
  {
    CLog::Log(LOGDEBUG, LOGVIDEO, "{} {:x}->{:x}",  __func__, m_codecControlFlags, flags);
    m_codecControlFlags = flags;

    if (flags & DVD_CODEC_CTRL_DROP)
      m_videobuffer.iFlags |= DVP_FLAG_DROPPED;
    else
      m_videobuffer.iFlags &= ~DVP_FLAG_DROPPED;

    if (m_codec)
      m_codec->setDrain((flags & DVD_CODEC_CTRL_DRAIN) != 0);
  }
}

void CDVDVideoCodecAmlogic::SetSpeed(int iSpeed)
{
  if (m_codec)
    m_codec->setSpeed(iSpeed);
}
