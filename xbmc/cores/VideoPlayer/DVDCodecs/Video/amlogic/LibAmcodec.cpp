/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "LibAmcodec.h"

#include "DynamicDll.h"
#include "PlatformDefs.h"

#include "utils/log.h"

#include <poll.h>

#define LIBAMCODEC	"/usr/osmc/lib/libamcodec.so"

using namespace amlogic;

// ----------------------------------------------------------------------------

class LibamCodecInterface
{
public:
  virtual ~LibamCodecInterface() {};

  virtual int codec_init(codec_para_t *pcodec)=0;
  virtual int codec_close(codec_para_t *pcodec)=0;
  virtual int codec_reset(codec_para_t *pcodec)=0;
  virtual int codec_pause(codec_para_t *pcodec)=0;
  virtual int codec_resume(codec_para_t *pcodec)=0;
  virtual int codec_write(codec_para_t *pcodec, void *buffer, int len)=0;
  virtual int codec_checkin_pts(codec_para_t *pcodec, unsigned long pts)=0;
  virtual int codec_checkin_pts64(codec_para_t *pcodec, unsigned long long pts64)=0;
  virtual int codec_get_vbuf_state(codec_para_t *pcodec, struct buf_status *buf)=0;
  virtual int codec_get_vdec_state(codec_para_t *pcodec, struct vdec_status *vdec)=0;

  virtual int codec_get_vdec_is_buffering(codec_para_t *pcodec, unsigned long *buffering)=0;

  virtual int codec_init_cntl(codec_para_t *pcodec)=0;
  virtual int codec_poll_cntl(codec_para_t *pcodec)=0;
  virtual int codec_set_cntl_mode(codec_para_t *pcodec, unsigned int mode)=0;
  virtual int codec_set_cntl_avthresh(codec_para_t *pcodec, unsigned int avthresh)=0;
  virtual int codec_set_cntl_syncthresh(codec_para_t *pcodec, unsigned int syncthresh)=0;

  virtual int codec_set_av_threshold(codec_para_t *pcodec, int threshold)=0;
  virtual int codec_set_video_delay_limited_ms(codec_para_t *pcodec,int delay_ms)=0;
  virtual int codec_get_video_delay_limited_ms(codec_para_t *pcodec,int *delay_ms)=0;
  virtual int codec_get_video_cur_delay_ms(codec_para_t *pcodec,int *delay_ms)=0;

  virtual void codec_set_log_callback(void (*logf)(const char *, ...))=0;
  virtual int codec_set_3d_video_mode(codec_para_t *pcodec, video_mode_3d_t mode)=0;
  virtual int codec_set_video_mode(codec_para_t *pcodec, video_input_mode vimode, video_output_mode vomode, int left_eye_first)=0;
  virtual int codec_set_hdr10p_metadata(codec_para_t *pcodec, void *metadata, int metadata_size)=0;
};

class amlogic::DllLibAmCodec : public DllDynamic, LibamCodecInterface
{
  DECLARE_DLL_WRAPPER(DllLibAmCodec, LIBAMCODEC)

  DEFINE_METHOD1(int, codec_init,               (codec_para_t *p1))
  DEFINE_METHOD1(int, codec_close,              (codec_para_t *p1))
  DEFINE_METHOD1(int, codec_reset,              (codec_para_t *p1))
  DEFINE_METHOD1(int, codec_pause,              (codec_para_t *p1))
  DEFINE_METHOD1(int, codec_resume,             (codec_para_t *p1))
  DEFINE_METHOD3(int, codec_write,              (codec_para_t *p1, void *p2, int p3))
  DEFINE_METHOD2(int, codec_checkin_pts,        (codec_para_t *p1, unsigned long p2))
  DEFINE_METHOD2(int, codec_checkin_pts64,      (codec_para_t *p1, unsigned long long p2))
  DEFINE_METHOD2(int, codec_get_vbuf_state,     (codec_para_t *p1, struct buf_status * p2))
  DEFINE_METHOD2(int, codec_get_vdec_state,     (codec_para_t *p1, struct vdec_status * p2))

  DEFINE_METHOD2(int, codec_get_vdec_is_buffering,     (codec_para_t *p1, unsigned long *p2))

  DEFINE_METHOD1(int, codec_init_cntl,          (codec_para_t *p1))
  DEFINE_METHOD1(int, codec_poll_cntl,          (codec_para_t *p1))
  DEFINE_METHOD2(int, codec_set_cntl_mode,      (codec_para_t *p1, unsigned int p2))
  DEFINE_METHOD2(int, codec_set_cntl_avthresh,  (codec_para_t *p1, unsigned int p2))
  DEFINE_METHOD2(int, codec_set_cntl_syncthresh,(codec_para_t *p1, unsigned int p2))

  DEFINE_METHOD2(int, codec_set_av_threshold,   (codec_para_t *p1, int p2))
  DEFINE_METHOD2(int, codec_set_video_delay_limited_ms, (codec_para_t *p1, int p2))
  DEFINE_METHOD2(int, codec_get_video_delay_limited_ms, (codec_para_t *p1, int *p2))
  DEFINE_METHOD2(int, codec_get_video_cur_delay_ms, (codec_para_t *p1, int *p2))

  DEFINE_METHOD_FP(void, codec_set_log_callback, (void (*)(const char *, ...)))
  DEFINE_METHOD2(int, codec_set_3d_video_mode,   (codec_para_t *p1, video_mode_3d_t p2))
  DEFINE_METHOD4(int, codec_set_video_mode,      (codec_para_t *p1, video_input_mode p2, video_output_mode p3, int p4))
  DEFINE_METHOD3(int, codec_set_hdr10p_metadata, (codec_para_t *p1, void *p2, int p3))

  BEGIN_METHOD_RESOLVE()
    RESOLVE_METHOD(codec_init)
    RESOLVE_METHOD(codec_close)
    RESOLVE_METHOD(codec_reset)
    RESOLVE_METHOD(codec_pause)
    RESOLVE_METHOD(codec_resume)
    RESOLVE_METHOD(codec_write)
    RESOLVE_METHOD(codec_checkin_pts)
    RESOLVE_METHOD(codec_checkin_pts64)
    RESOLVE_METHOD(codec_get_vbuf_state)
    RESOLVE_METHOD(codec_get_vdec_state)

    RESOLVE_METHOD_OPTIONAL(codec_get_vdec_is_buffering)

    RESOLVE_METHOD(codec_init_cntl)
    RESOLVE_METHOD(codec_poll_cntl)
    RESOLVE_METHOD(codec_set_cntl_mode)
    RESOLVE_METHOD(codec_set_cntl_avthresh)
    RESOLVE_METHOD(codec_set_cntl_syncthresh)

    RESOLVE_METHOD(codec_set_av_threshold)
    RESOLVE_METHOD(codec_set_video_delay_limited_ms)
    RESOLVE_METHOD(codec_get_video_delay_limited_ms)
    RESOLVE_METHOD(codec_get_video_cur_delay_ms)

    RESOLVE_METHOD_OPTIONAL_FP(codec_set_log_callback)
    RESOLVE_METHOD(codec_set_3d_video_mode)
    RESOLVE_METHOD(codec_set_video_mode)
    RESOLVE_METHOD(codec_set_hdr10p_metadata)
  END_METHOD_RESOLVE()

public:
	void codec_set_log_callback(void (*logf)(const char*, ...))
	{
		if (m_codec_set_log_callback_ptr) {
			m_codec_set_log_callback(logf);
		}
	}

	bool IsVCodecBuffering(codec_para_t *param)
	{
		unsigned long parm = 0;

		if (m_codec_get_vdec_is_buffering_ptr == nullptr
				|| codec_get_vdec_is_buffering(param, &parm)) {
			return false;
		}

		return parm != 0;
	}

};

// ----------------------------------------------------------------------------

LibAmcodec::LibAmcodec()
{
	m_dll = new DllLibAmCodec();
	if (!m_dll->Load()) {
		CLog::Log(LOGWARNING, "LibAmcodec::LibAmcodec " LIBAMCODEC " not found");
		delete m_dll, m_dll = nullptr;
	}

	m_codec = new codec_para_t;
	m_codec->handle             = -1; //init to invalid
	m_codec->cntl_handle        = -1;
	m_codec->sub_handle         = -1;
	m_codec->audio_utils_handle = -1;
}

LibAmcodec::~LibAmcodec()
{
	delete m_codec, m_codec = nullptr;

	delete m_dll, m_dll = nullptr;
}

int LibAmcodec::checkinPts64(uint64_t pts64)
{
	return m_dll->codec_checkin_pts64(m_codec, pts64);
}

void LibAmcodec::setLogCallback(void (*logf)(const char *, ...))
{
	m_dll->codec_set_log_callback(logf);
}

int LibAmcodec::init(const aml_generic_param &gparam)
{
	memset(m_codec, 0x00, sizeof(codec_para_t));

	// direct struct usage, we do not know which flavor
	// so just use what we get from headers and pray.
	m_codec->handle				= -1; //init to invalid
	m_codec->cntl_handle		= -1;
	m_codec->sub_handle			= -1;
	m_codec->audio_utils_handle	= -1;
	m_codec->has_video			= 1;
	m_codec->noblock			= gparam.noblock;
	m_codec->video_pid			= gparam.video_pid;
	m_codec->video_type			= gparam.video_type;
	m_codec->video_path			= gparam.video_path;
	m_codec->stream_type		= gparam.stream_type;
	m_codec->decoder_type		= gparam.decoder_type;
	m_codec->display_mode		= gparam.display_mode;
	m_codec->config				= gparam.config;
	m_codec->config_len			= gparam.config_len;
	m_codec->am_sysinfo.format	= gparam.format;
	m_codec->am_sysinfo.width	= gparam.width;
	m_codec->am_sysinfo.height	= gparam.height;
	m_codec->am_sysinfo.rate	= gparam.rate;
	m_codec->am_sysinfo.extra	= gparam.extra;
	m_codec->am_sysinfo.status	= gparam.status;
	m_codec->am_sysinfo.ratio	= gparam.ratio;
	m_codec->am_sysinfo.ratio64	= gparam.ratio64;
	m_codec->am_sysinfo.param	= gparam.param;

	return m_dll->codec_init(m_codec);
}

int LibAmcodec::pause()
{
	return m_dll->codec_pause(m_codec);
}

int LibAmcodec::close()
{
	return m_dll->codec_close(m_codec);
}

int LibAmcodec::reset()
{
	return m_dll->codec_reset(m_codec);
}

int LibAmcodec::setControlMode(unsigned int mode)
{
	return m_dll->codec_set_cntl_mode(m_codec, mode);
}

int LibAmcodec::setVideoDelayLimitedMs(int delay)
{
	return m_dll->codec_set_video_delay_limited_ms(m_codec, delay);
}

int LibAmcodec::setControlAvThreshold(unsigned int threshold)
{
	return m_dll->codec_set_cntl_avthresh(m_codec, threshold);
}

int LibAmcodec::setControlSyncThreshold(unsigned int threshold)
{
	return m_dll->codec_set_cntl_syncthresh(m_codec, threshold);
}

int LibAmcodec::getVbufState(struct buf_status &bs) const
{
	return m_dll->codec_get_vbuf_state(m_codec, &bs);
}

bool LibAmcodec::isVCodecBuffering() const
{
	return m_dll->IsVCodecBuffering(m_codec);
}

int LibAmcodec::set3dVideoMode(video_mode_3d_t mode)
{
	return m_dll->codec_set_3d_video_mode(m_codec, mode);
}

int LibAmcodec::setVideoMode(video_input_mode vimode, video_output_mode vomode, bool leftEyeFirst)
{
	return m_dll->codec_set_video_mode(m_codec, vimode, vomode, leftEyeFirst ? 1 : 0);
}

int LibAmcodec::setHdr10pMetadata(unsigned char *metadata, int metadata_length)
{
	return m_dll->codec_set_hdr10p_metadata(m_codec, metadata, metadata_length);
}

int LibAmcodec::getVdecState(struct vdec_status &vs) const
{
	return m_dll->codec_get_vdec_state(m_codec, &vs);
}

int LibAmcodec::write(unsigned char *buf, unsigned int size)
{
	return m_dll->codec_write(m_codec, buf, (int) size);
}

int LibAmcodec::poll(unsigned int timeoutMs)
{
	struct pollfd codec_poll_fd[1];

	codec_poll_fd[0].fd = m_codec->cntl_handle;
	codec_poll_fd[0].events = POLLOUT;

	if (::poll(codec_poll_fd, 1, timeoutMs) > 0) {
		return 1;
	}

	return 0;
}

void LibAmcodec::setNoBlockMode(bool noblock)
{
	m_codec->noblock = noblock ? 1 : 0;
}

void LibAmcodec::setStreamType(stream_type_t type)
{
	m_codec->stream_type = type;
}
