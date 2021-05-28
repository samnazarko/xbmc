/*
 *  Copyright (C) 2007-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "RendererAML.h"

#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecAmlogic.h"
#include "cores/VideoPlayer/DVDCodecs/Video/AMLCodec.h"
#include "utils/log.h"
#include "utils/SysfsUtils.h"
#include "utils/ScreenshotAML.h"
#include "settings/MediaSettings.h"
#include "cores/VideoPlayer/VideoRenderers/RenderCapture.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFactory.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"

#include "windowing/GraphicContext.h"
#include "ServiceBroker.h"

#include <chrono>
#include <thread>

CRendererAML::CRendererAML()
 : m_prevVPts(-1)
 , m_bConfigured(false)
 , m_sleepDurationInMs(0)
{
  CLog::Log(LOGINFO, "Constructing CRendererAML");
}

CRendererAML::~CRendererAML()
{
  Reset();
}

CBaseRenderer* CRendererAML::Create(CVideoBuffer *buffer)
{
  if (buffer && dynamic_cast<CAMLVideoBuffer*>(buffer))
    return new CRendererAML();
  return nullptr;
}

bool CRendererAML::Register()
{
  VIDEOPLAYER::CRendererFactory::RegisterRenderer("amlogic", CRendererAML::Create);
  return true;
}

float CRendererAML::GetVideoRefreshRate() const
{
  RESOLUTION res = CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution();
  const RESOLUTION_INFO& video_res_info = CDisplaySettings::GetInstance().GetResolutionInfo(res);

  return video_res_info.fRefreshRate;
}

bool CRendererAML::Configure(const VideoPicture &picture, float fps, unsigned int orientation)
{
  m_sourceWidth = picture.iWidth;
  m_sourceHeight = picture.iHeight;
  m_renderOrientation = orientation;

  // Calculate the thread sleep duration in ms.
  //
  // The render thread is used to sleep between frames. This keeps the CPU load and CPU
  // temperature low. When polling is being used to let the thread sleep until the next
  // vsync event occurs, the Linux scheduler wakeup latency might be too high (around 6 ms)
  // and could cause some glitches like frame skips. Further, that thread needs another (up
  // to) 6 ms to prepare the next frame before it is being displayed. This gives a total
  // (worst case) latency of about 12 ms.
  //
  // The resulting sleep duration depends on the fps. For instance, for 24 fps (42 ms per
  // frame) the thread should sleep for 42 - 6 - 6 = 30 ms. BUT: if Kodi sets a different
  // refresh rate (eg. 50 Hz instead of 25 Hz) we must rely on that refresh rate.
  //
  // It doesn't matter that the thread doesn't send a frame exactly every e.g. 42 ms. The
  // kernel will display frames only when a vsync event occurs. We just have to make sure
  // that there's at least one frame available when the kernel needs it.
  m_sleepDurationInMs = int(1000.0 / GetVideoRefreshRate() - 0.5) - 6 - 6;
  if (m_sleepDurationInMs < 2) {
	  // if the sleep duration is too low, we do not sleep at all
	  m_sleepDurationInMs = 0;
  }

  m_iFlags = GetFlagsChromaPosition(picture.chroma_position) |
             GetFlagsColorMatrix(picture.color_space, picture.iWidth, picture.iHeight) |
             GetFlagsColorPrimaries(picture.color_primaries) |
             GetFlagsStereoMode(picture.stereoMode);

  // Calculate the input frame aspect ratio.
  CalculateFrameAspectRatio(picture.iDisplayWidth, picture.iDisplayHeight);
  SetViewMode(m_videoSettings.m_ViewMode);
  ManageRenderArea();

  m_bConfigured = true;

  return true;
}

CRenderInfo CRendererAML::GetRenderInfo()
{
  CRenderInfo info;
  info.max_buffer_size = m_numRenderBuffers;
  info.optimal_buffer_size = m_numRenderBuffers;
  info.opaque_pointer = (void *)this;
  return info;
}

bool CRendererAML::RenderCapture(int index, CRenderCapture* capture)
{
  capture->BeginRender();
  capture->EndRender();
  CScreenshotAML::CaptureVideoFrame((unsigned char *)capture->GetRenderBuffer(), capture->GetWidth(), capture->GetHeight(), false);
  return true;
}

void CRendererAML::AddVideoPicture(const VideoPicture &picture, int index)
{
  ReleaseBuffer(index);

  BUFFER &buf(m_buffers[index]);
  if (picture.videoBuffer)
  {
    buf.videoBuffer = picture.videoBuffer;
    buf.videoBuffer->Acquire();
  }
}

void CRendererAML::ReleaseBuffer(int idx)
{
  BUFFER &buf(m_buffers[idx]);
  if (buf.videoBuffer)
  {
    CAMLVideoBuffer *amli(dynamic_cast<CAMLVideoBuffer*>(buf.videoBuffer));
    if (amli)
    {
      if (amli->m_amlCodec)
      {
        amli->m_amlCodec->ReleaseFrame(amli->m_bufferIndex, true);
        amli->m_amlCodec = nullptr; // Released
      }
      amli->Release();
    }
    buf.videoBuffer = nullptr;
  }
}

bool CRendererAML::Supports(ERENDERFEATURE feature) const
{
  if (feature == RENDERFEATURE_ZOOM ||
      feature == RENDERFEATURE_CONTRAST ||
      feature == RENDERFEATURE_BRIGHTNESS ||
      feature == RENDERFEATURE_NONLINSTRETCH ||
      feature == RENDERFEATURE_VERTICAL_SHIFT ||
      feature == RENDERFEATURE_STRETCH ||
      feature == RENDERFEATURE_PIXEL_RATIO ||
      feature == RENDERFEATURE_ROTATION)
    return true;

  return false;
}

void CRendererAML::Reset()
{
  std::array<int, 2> reset_arr[m_numRenderBuffers];
  m_prevVPts = -1;

  for (int i = 0 ; i < m_numRenderBuffers ; ++i)
  {
    reset_arr[i][0] = i;

    if (m_buffers[i].videoBuffer)
      reset_arr[i][1] = dynamic_cast<CAMLVideoBuffer *>(m_buffers[i].videoBuffer)->m_bufferIndex;
    else
      reset_arr[i][1] = 0;
  }

  std::sort(std::begin(reset_arr), std::end(reset_arr),
    [](const std::array<int, 2>& u, const std::array<int, 2>& v)
    {
      return u[1] < v[1];
    });

  for (int i = 0; i < m_numRenderBuffers; ++i)
  {
    if (m_buffers[reset_arr[i][0]].videoBuffer)
    {
      m_buffers[reset_arr[i][0]].videoBuffer->Release();
      m_buffers[reset_arr[i][0]].videoBuffer = nullptr;
    }
  }
}

bool CRendererAML::Flush(bool saveBuffers)
{
  Reset();
  return saveBuffers;
};

void CRendererAML::RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha)
{
  ManageRenderArea();

  CAMLVideoBuffer *amli = dynamic_cast<CAMLVideoBuffer *>(m_buffers[index].videoBuffer);
  if(amli && amli->m_amlCodec)
  {
    int pts = amli->m_omxPts;
    if (pts != m_prevVPts)
    {
      amli->m_amlCodec->ReleaseFrame(amli->m_bufferIndex);
      amli->m_amlCodec->SetVideoRect(m_sourceRect, m_destRect);
      amli->m_amlCodec = nullptr; //Mark frame as processed
      m_prevVPts = pts;
    }
  }

#if 0
  // Use polling to sync with the kernel's vsync IRQ. It turned out that the
  // Linux schedulers wakeup latency varies too much. Wakeup delays of up to 6 ms
  // are too big and are causing 'stutters' like skipped/dropped frames. However,
  // this can be avoided if we let Kodi run with realtime priority.
  //
  CAMLCodec::PollFrame();
#else
  // We do not use polling in this case. The vsync IRQ will handle 'ready' frames anyway.
  // Just sleep a bit here to avoid high CPU usage and temperature.
  //
  // Note: you want to use polling if you enable the 'sync-to-display' option.
  //
  std::this_thread::sleep_for(std::chrono::milliseconds(m_sleepDurationInMs));
#endif
}
