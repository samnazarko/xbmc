/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <deque>
#include <string>
#include <list>

#include "cores/VideoPlayer/DVDStreamInfo.h"
#include "cores/VideoSettings.h"
#include "utils/Geometry.h"
#include "rendering/RenderSystem.h"
#include "windowing/Resolution.h"

extern "C" {
	#include <amcodec/codec.h>
}

#include "AMLVideoCodec.h"

#include "AMLVideoCodecDataTypes.h"

class OSMCSecureOS;

typedef struct am_packet am_packet_t;
typedef std::shared_ptr<amlogic::PosixFile> PosixFilePtr;

class amlogic::AMLInsecureVideoCodec : public amlogic::AMLVideoCodec
{
private:
	int m_sleepDurationInMs;

	void am_packet_release(am_packet_t *pkt);

	float getTimeSize();
	int dequeueBuffer();

	void setVideoContrast(const int contrast) const;
	void setVideoBrightness(const int brightness) const;

	void setFramepackingResolution(const int width, const int height, const int blanking) const;
	void setFramepackingSupport(const bool enable) const;
	void setMvcViewMode(const int viewMode) const;

	int getVideoContrast() const;
	int getVideoBrightness() const;
	unsigned int getDecoderVideoRate() const;

	int check_in_pts(am_private_t *para, am_packet_t *pkt) const;
	int write_av_packet(am_private_t *para, am_packet_t *pkt) const;

	int handle_pre_header_feeding(am_private_t *para, am_packet_t *pkt) const;

protected:
	amlogic::LibAmcodec	*m_libamcodec;

	am_private_t		*m_am_private;

	bool				 m_opened;
	int					 m_speed;
	uint64_t			 m_cur_pts;
	uint64_t			 m_last_pts;
	uint32_t			 m_bufferIndex;
	unsigned int		 m_state;
	std::uint32_t		 m_frameSizeSum;
	std::deque<uint32_t> m_frameSizes;

	bool			 m_drain = false;
	CRect			 m_dst_rect;
	CRect			 m_display_rect;
	float			 m_zoom = -1.0f;
	int				 m_contrast = -1;
	int				 m_brightness = -1;
	int				 m_orientation = 0;
	CDVDStreamInfo	 m_hints;

	int					 m_view_mode = -1;
	RENDER_STEREO_MODE	 m_guiStereoMode = RENDER_STEREO_MODE_OFF;
	RENDER_STEREO_VIEW	 m_guiStereoView = RENDER_STEREO_VIEW_OFF;
	RESOLUTION			 m_video_res = RES_INVALID;
	std::string			 m_video_stereo_mode;

	PosixFilePtr	 m_amlVideoFile;
	std::string		 m_defaultVfmMap;

	OSMCSecureOS	*m_secureOSMC;

	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const = 0;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const = 0;

	virtual bool openAmlVideo(const CDVDStreamInfo &hints);
	virtual void closeAmlVideo();

	virtual void showMainVideo(const bool show);
	virtual void setVfmMap(const std::string &name, const std::string &map);

	virtual std::string getVfmMap(const std::string &name) const;
	virtual void am_packet_init(am_packet_t *pkt) const;
	virtual pstream_type getStreamType() const;
	virtual unsigned int getVideoRate(const CDVDStreamInfo &hints) const;
	virtual void getExtradata(const CDVDStreamInfo &hints, uint8_t *&extradata, unsigned int &extrasize) const;
	virtual bool handleMasteringMetadata(const CDVDStreamInfo &hints) const;
	virtual void enableCustomMasterDataDisplay(bool enable = true) const;
	virtual void getDeinterlacingMethods(std::list<EINTERLACEMETHOD> &methods) const;
	virtual void setupVideoCodecParams(aml_generic_param &params) const;

	virtual int pre_header_feeding(am_private_t *para, am_packet_t *pkt) const;
	virtual int set_header_info(am_private_t *para) const;

	virtual int write_header(am_private_t *para, am_packet_t *pkt) const;

	virtual int poll();
	virtual int getSleepDurationInMs() const;
	virtual int calculateSleepDuration();

	virtual void playbackPause();
	virtual void playbackResume();
	virtual void playbackResume(int speed);

public:
	AMLInsecureVideoCodec(CProcessInfo &processInfo);
	virtual ~AMLInsecureVideoCodec();

	virtual bool openDecoder(CDVDStreamInfo &hints);
	virtual void closeDecoder();

	virtual int getOMXPts() const;
	virtual uint32_t getBufferIndex() const;
	virtual int getAmlDuration() const;

	virtual double getAspectRatio() const;

	virtual bool prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts);
	virtual bool addData(uint8_t *pData, size_t iSize, double dts, double pts, uint8_t subtitlePlane);
	virtual CDVDVideoCodec::VCReturn getPicture(VideoPicture *pVideoPicture);
	virtual int releaseFrame(const uint32_t index, bool drop=false);

	virtual void setVideoRect(const CRect &SrcRect, const CRect &DestRect);

	virtual void setSpeed(int speed);
	virtual void setDrain(bool drain);

	virtual void reset();
};
