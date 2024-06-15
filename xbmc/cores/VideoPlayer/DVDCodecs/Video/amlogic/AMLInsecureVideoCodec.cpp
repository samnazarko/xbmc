/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AMLVideoCodecConstants.h"
#include "AMLVideoCodecDataTypes.h"

#include "LibAmcodec.h"
#include "PosixFile.h"

#include "OSMCSecureOS.h"

#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"
#include "cores/VideoPlayer/DVDStreamInfo.h"
#include "utils/log.h"
#include "utils/SysfsUtils.h"
#include "utils/AMLUtils.h"
#include "utils/StringUtils.h"
#include "utils/MathUtils.h"
#include "settings/Settings.h"
#include "settings/DisplaySettings.h"
#include "settings/SettingsComponent.h"
#include "windowing/GraphicContext.h"
#include "windowing/Resolution.h"
#include "guilib/GUIComponent.h"
#include "guilib/StereoscopicsManager.h"

#include "ServiceBroker.h"

#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <thread>
#include <chrono>

#include "AMLInsecureVideoCodec.h"

#include "AMLDecoderInputQueueStats.h"

extern "C" {
	#include <libavcodec/packet.h>
}

using namespace std::chrono_literals;
using namespace amlogic;

CEvent g_aml_codec_sync_event;

static const uint64_t UINT64_0 = 0x8000000000000000ULL;

static const int RW_WAIT_TIME = 5 * 1000; // 5 ms

static const unsigned int STATE_HASPTS = 2;

// max level for the decoder input queue
static const float maxDecoderInputLevel = 0.7f;

static const int drainRepeatCount = 20;

/*************************************************************************/

extern "C" void libamlcodec_logger(const char *format, ...)
{
  va_list arglist;
  char buf[512];

  va_start(arglist, format);
  vsnprintf(buf, sizeof(buf), format, arglist);
  va_end(arglist);

  CLog::Log(LOGINFO, buf);
}

/*************************************************************************/

AMLInsecureVideoCodec::AMLInsecureVideoCodec(CProcessInfo &processInfo)
	: AMLVideoCodec(processInfo), m_opened(false), m_speed(DVD_PLAYSPEED_NORMAL), m_cur_pts(DVD_NOPTS_VALUE),
	  m_last_pts(DVD_NOPTS_VALUE), m_bufferIndex(-1), m_state(0), m_filling(true), m_secureOSMC(&OSMCSecureOS::getInstance())
{
	m_am_private = new am_private_t;
	memset(m_am_private, 0, sizeof(am_private_t));

	m_input_queue_length = 0;

	m_libamcodec = new LibAmcodec();
	m_decInputQueue = new AMLDecoderInputQueueStats(m_libamcodec);

	// set logging callback
	m_libamcodec->setLogCallback(libamlcodec_logger);
}

AMLInsecureVideoCodec::~AMLInsecureVideoCodec()
{
	delete m_am_private, m_am_private = nullptr;
	delete m_decInputQueue, m_decInputQueue = nullptr;
	delete m_libamcodec, m_libamcodec = nullptr;
}

int AMLInsecureVideoCodec::poll()
{
	if (m_libamcodec == nullptr) {
		// no codec, no reason to poll
		return 0;
	}

	std::chrono::time_point<std::chrono::system_clock> now(std::chrono::system_clock::now());

	if (m_libamcodec->poll(50)) {
		g_aml_codec_sync_event.Set();
		int elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - now).count();
		CLog::Log(LOGDEBUG, LOGAVTIMING, "AMLInsecureVideoCodec::pollFrame elapsed:{:0.3f}ms", elapsed / 1000.0);
		return 1;
	}
	return 0;
}

void AMLInsecureVideoCodec::setVfmMap(const std::string &name, const std::string &map)
{
	SysfsUtils::SetString("/sys/class/vfm/map", "rm " + name);
	SysfsUtils::SetString("/sys/class/vfm/map", "add " + name + " " + map);
}

std::string AMLInsecureVideoCodec::getVfmMap(const std::string &name) const
{
	std::string vfmMap;
	SysfsUtils::GetString("/sys/class/vfm/map", vfmMap);
	std::vector < std::string > sections = StringUtils::Split(vfmMap, '\n');
	std::string sectionMap;

	for (size_t i = 0; i < sections.size(); ++i) {
		if (StringUtils::StartsWith(sections[i], name + " {")) {
			sectionMap = sections[i];
			break;
		}
	}

	int openingBracePos = sectionMap.find('{') + 1;
	sectionMap = sectionMap.substr(openingBracePos,
			sectionMap.size() - openingBracePos - 1);
	StringUtils::Replace(sectionMap, "(0)", "");

	return sectionMap;
}

bool AMLInsecureVideoCodec::openAmlVideo(const CDVDStreamInfo &hints)
{
	PosixFilePtr amlVideoFile = std::make_shared<PosixFile>();
	if (!amlVideoFile->Open("/dev/video10", O_RDONLY | O_NONBLOCK)) {
		CLog::Log(LOGERROR, "AMLInsecureVideoCodec::openAmlVideo - cannot open V4L amlvideo device /dev/video10: {}", strerror(errno));
		return false;
	}

	m_amlVideoFile = amlVideoFile;

	m_defaultVfmMap = getVfmMap("default");

	setVfmMap("default", "decoder ppmgr deinterlace amlvideo amvideo");

	int hdrMode = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_HDR_PROCESS);

	SysfsUtils::SetInt("/sys/module/am_vecm/parameters/hdr_mode", hdrMode);

	int maxLum = CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOSCREEN_MAXLUM);
	SysfsUtils::SetInt("/sys/module/am_vecm/parameters/customer_panel_lumin", maxLum);

	SysfsUtils::SetInt("/sys/class/video/freerun_mode", 3);

	return true;
}

void AMLInsecureVideoCodec::closeAmlVideo()
{
	m_amlVideoFile.reset();
	setVfmMap("default", m_defaultVfmMap);
	m_amlVideoFile = nullptr;
}

void AMLInsecureVideoCodec::showMainVideo(const bool show)
{
  static int saved_disable_video = -1;

  int disable_video = show ? 0:1;
  if (saved_disable_video == disable_video)
    return;

  SysfsUtils::SetInt("/sys/class/video/disable_video", disable_video);
  saved_disable_video = disable_video;
}

void AMLInsecureVideoCodec::am_packet_init(am_packet_t *pkt) const
{
  memset(&pkt->avpkt, 0, sizeof(AVPacket));
  pkt->avpts      = 0;
  pkt->avdts      = 0;
  pkt->avduration = 0;
  pkt->isvalid    = 0;
  pkt->newflag    = 0;
  pkt->lastpts    = UINT64_0;
  pkt->data       = NULL;
  pkt->buf        = NULL;
  pkt->data_size  = 0;
  pkt->buf_size   = 0;
  pkt->hdr        = new hdr_buf;
}

pstream_type AMLInsecureVideoCodec::getStreamType() const
{
	return AM_STREAM_ES;
}

unsigned int AMLInsecureVideoCodec::getVideoRate(const CDVDStreamInfo &hints) const
{
	unsigned int video_rate = 0;

	if (hints.fpsrate > 0 && hints.fpsscale != 0) {
		// then ffmpeg avg_frame_rate next
		video_rate = 0.5 + (float) UNIT_FREQ * hints.fpsscale / hints.fpsrate;
	} else {
		// the video_rate can't be calculated because fpsrate and/or fpsscale are invalid, so
		// we're using a default one as a fallback
		video_rate = 0.5 + (float) UNIT_FREQ * 1001 / 30000;
	}

	// check for 1920x1080, interlaced, 25 fps
	// incorrectly reported as 50 fps (yes, video_rate == 1920)
	if (hints.width == 1920 && video_rate == 1920 && hints.interlaced) {
		CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::getVideoRate: video_rate exception");
		video_rate = 0.5 + (float) UNIT_FREQ * 1000 / 25000;
	}

	return video_rate;
}

void AMLInsecureVideoCodec::getExtradata(const CDVDStreamInfo &hints, uint8_t *&extradata, unsigned int &extrasize) const
{
	if (hints.extradata.GetSize() == 0) {
		extradata = nullptr;
		extrasize = 0;
		return;
	}

	extradata = (uint8_t*)malloc(hints.extradata.GetSize());
	extrasize = hints.extradata.GetSize();
	memcpy(extradata, hints.extradata.GetData(), hints.extradata.GetSize());
}

bool AMLInsecureVideoCodec::handleMasteringMetadata(const CDVDStreamInfo &hints) const
{
	char customMastering[256];
	int len = 0;

	if (hints.colorTransferCharacteristic > 3) {
		len = snprintf(customMastering, 256, "%d,%d", hints.colorPrimaries, hints.colorTransferCharacteristic);
	}

	if (hints.masteringMetadata) {
		int i, j;
		if (hints.masteringMetadata->has_primaries) {
			/* we have found headers with metadata differing from standard values in the 5th decimal place */
			CLog::Log(LOGDEBUG,
					"AMLInsecureVideoCodec::handleMasteringMetadata Mastering Display Primaries Red: {:.5f} {:.5f} Green: {:.5f} {:.5f} Blue: {:.5f} {:.5f} White: {:.5f} {:.5f}",
					av_q2d(hints.masteringMetadata->display_primaries[0][0]),
					av_q2d(hints.masteringMetadata->display_primaries[0][1]),
					av_q2d(hints.masteringMetadata->display_primaries[1][0]),
					av_q2d(hints.masteringMetadata->display_primaries[1][1]),
					av_q2d(hints.masteringMetadata->display_primaries[2][0]),
					av_q2d(hints.masteringMetadata->display_primaries[2][1]),
					av_q2d(hints.masteringMetadata->white_point[0]),
					av_q2d(hints.masteringMetadata->white_point[1]));
			for (i = 0; i < 3; i++) {
				for (j = 0; j < 2; j++) {
					/* all standard primary triads are no more than 3 dec places */
					len += snprintf(customMastering + len, 256 - len, ",%d",
						(int) ((av_q2d(hints.masteringMetadata->display_primaries[(i + 1) % 3][j]) + 0.0005) * 1000) * 50);
				}
			}
			for (i = 0; i < 2; i++) {
				/* white points are up to 4 dec places */
				len += snprintf(customMastering + len, 256 - len, ",%d",
					(int) ((av_q2d(hints.masteringMetadata->white_point[i]) + 0.00005) * 10000) * 5);
			}
		} else {
			CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::handleMasteringMetadata No mastering primaries found");
			for (i = 0; i < 7; i++) {
				len += snprintf(customMastering + len, 256 - len, ",0");
			}
		}

		if (hints.masteringMetadata->has_luminance) {
			CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::handleMasteringMetadata Max mastering luminance: {:.0f}, Min mastering luminance {:.4f}",
					av_q2d(hints.masteringMetadata->max_luminance), av_q2d(hints.masteringMetadata->min_luminance));
			len += snprintf(customMastering + len, 256 - len, ",%ld,%d",
				(long) (av_q2d(hints.masteringMetadata->max_luminance) + 0.5) * 10000,
				(int) (av_q2d(hints.masteringMetadata->min_luminance) * 10000 + 0.5));
		} else {
			CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::handleMasteringMetadata No mastering luminances found");
			len += snprintf(customMastering + len, 256 - len, ",0,0");
		}

		if (hints.contentLightMetadata) {
			CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::handleMasteringMetadata Content Light Levels MaxCLL: {}, MaxFALL {}",
					hints.contentLightMetadata->MaxCLL, hints.contentLightMetadata->MaxFALL);
			len += snprintf(customMastering + len, 256 - len, ",%d,%d",
					hints.contentLightMetadata->MaxCLL, hints.contentLightMetadata->MaxFALL);
		} else {
			CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::handleMasteringMetadata No content light levels in stream header");
			len += snprintf(customMastering + len, 256 - len, ",0,0");
		}
	}

	if (len > 0) {
		CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::handleMasteringMetadata Custom mastering data {}", customMastering);
		SysfsUtils::SetString("/sys/module/am_vecm/parameters/customer_master_display_param", customMastering);

		if (hints.colorTransferCharacteristic == AVCOL_TRC_ARIB_STD_B67) { // ARIB STD-B67, known as "Hybrid log-gamma"
			enableCustomMasterDataDisplay();
		}
	}

	return len > 0;
}

void AMLInsecureVideoCodec::enableCustomMasterDataDisplay(bool enable) const
{
	SysfsUtils::SetInt("/sys/module/am_vecm/parameters/customer_master_display_en", enable ? 1 : 0);
}

void AMLInsecureVideoCodec::getDeinterlacingMethods(std::list<EINTERLACEMETHOD> &methods) const
{
	methods.clear();
}

std::string AMLInsecureVideoCodec::getCodecPrivateConfiguration() const
{
	// nothing to configure yet
	return "";
}

void AMLInsecureVideoCodec::setupVideoCodecParams(aml_generic_param &params) const
{
	params.noblock      = 0;
	params.video_pid    = m_am_private->video_pid;
	params.video_type   = m_am_private->video_format;
	params.stream_type  = STREAM_TYPE_ES_VIDEO;
	params.decoder_type = DECODER_TYPE_SINGLE_MODE;
	params.video_path   = FRAME_BASE_PATH_AMVIDEO;
	params.display_mode	= DISPLAY_MODE_AMVIDEO;
	params.format       = m_am_private->video_codec_type;
	params.width        = m_am_private->video_width;
	params.height       = m_am_private->video_height;
	params.rate         = m_am_private->video_rate;
	params.ratio        = m_am_private->video_ratio;
	params.ratio64      = m_am_private->video_ratio64;
	params.param        = NULL;
	params.use_dv_vpath = false;
}

int AMLInsecureVideoCodec::handle_pre_header_feeding(am_private_t *para, am_packet_t *pkt) const
{
	pkt->hdr->alloc(HDR_BUF_SIZE);
	if (!pkt->hdr->data) {
		//CLog::Log(LOGDEBUG, "[pre_header_feeding] NOMEM!");
		return PLAYER_NOMEM;
	}

	int ret = pre_header_feeding(para, pkt);
	if (ret == PLAYER_SUCCESS && !pkt->hdr->isEmpty()) {
		// we write the header only if we were able to create it
		ret = write_av_packet(para, pkt);
	}

	pkt->hdr->free();

	return ret;
}

int AMLInsecureVideoCodec::pre_header_feeding(am_private_t *para, am_packet_t *pkt) const
{
	pkt->hdr->size = 0; // no header => no size

	return PLAYER_SUCCESS;
}

void AMLInsecureVideoCodec::playbackPause()
{
	m_libamcodec->setControlMode(TRICKMODE_NONE);
}

void AMLInsecureVideoCodec::playbackResume()
{
	m_libamcodec->setControlMode(TRICKMODE_NONE);
}

void AMLInsecureVideoCodec::playbackResume(int speed)
{
	m_libamcodec->setControlMode(TRICKMODE_I);
}

void AMLInsecureVideoCodec::setSpeed(int speed)
{
	if (m_speed == speed) {
		return;
	}

	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::SetSpeed, speed({})", speed);

	// update internal vars regardless of if we are open or not.
	m_speed = speed;

	if (!m_opened) {
		return;
	}

	switch (speed) {
		case DVD_PLAYSPEED_PAUSE:
			playbackPause();
			break;
		case DVD_PLAYSPEED_NORMAL:
			playbackResume();
			m_libamcodec->setControlMode(TRICKMODE_NONE);
			break;
		default:
			playbackResume(speed);
			break;
	}
}

bool AMLInsecureVideoCodec::openDecoder(CDVDStreamInfo &hints)
{
	m_speed = DVD_PLAYSPEED_NORMAL;
	m_drain = false;
	m_cur_pts = DVD_NOPTS_VALUE;
	m_dst_rect.SetRect(0, 0, 0, 0);
	m_zoom = -1.0f;
	m_contrast = -1;
	m_brightness = -1;
	m_hints = hints;
	m_state = 0;
	m_filling = true;
	m_decInputQueue->reset();

	if (!openAmlVideo(hints)) {
		CLog::Log(LOGERROR, "AMLInsecureVideoCodec::openDecoder - cannot open amlvideo device");
		return false;
	}

	showMainVideo(false);

	am_packet_init(&m_am_private->am_pkt);

	// default stream type
	m_am_private->stream_type = getStreamType();

	// handle hints.
	m_am_private->video_width = hints.width;
	m_am_private->video_height = hints.height;
	m_am_private->video_codec_id = hints.codec;
	m_am_private->video_codec_tag = hints.codec_tag;

	// handle video ratio
	AVRational video_ratio = av_d2q(1, SHRT_MAX);
	m_am_private->video_ratio = ((int32_t) video_ratio.num << 16) | video_ratio.den;
	m_am_private->video_ratio64 = ((int64_t) video_ratio.num << 32) | video_ratio.den;

	// handle video rate
	m_am_private->video_rate = getVideoRate(hints);

	// handle orientation
	m_am_private->video_rotation_degree = 0;
	if (hints.orientation == 90) {
		m_am_private->video_rotation_degree = 1;
	} else if (hints.orientation == 180) {
		m_am_private->video_rotation_degree = 2;
	} else if (hints.orientation == 270) {
		m_am_private->video_rotation_degree = 3;
	}

	m_am_private->video_format = getVideoFormat(hints);
	m_am_private->video_codec_type = getVideoCodecType(hints);

	getExtradata(hints, m_am_private->extradata, m_am_private->extrasize);

	CLog::Log(LOGDEBUG,
		"AMLInsecureVideoCodec::openDecoder hints.width({}), hints.height({}), hints.codec({}), hints.codec_tag({:4.4s})",
		hints.width, hints.height, hints.codec, (char*) &hints.codec_tag);
	CLog::Log(LOGDEBUG,
		"AMLInsecureVideoCodec::openDecoder hints.fpsrate({}), hints.fpsscale({}), video_rate({}), hints.bitsperpixel({})",
		hints.fpsrate, hints.fpsscale, m_am_private->video_rate, hints.bitsperpixel);
	CLog::Log(LOGDEBUG,
		"AMLInsecureVideoCodec::openDecoder hints.aspect({:f}), video_ratio.num({}), video_ratio.den({})",
		hints.aspect, video_ratio.num, video_ratio.den);
	CLog::Log(LOGDEBUG,
		"AMLInsecureVideoCodec::openDecoder hints.orientation({}), hints.forced_aspect({}), hints.extrasize({})",
		hints.orientation, hints.forced_aspect, hints.extradata.GetSize());
	CLog::Log(LOGDEBUG,"AMLInsecureVideoCodec::openDecoder hints.interlaced({})", hints.interlaced);
	CLog::Log(LOGDEBUG,
		"AMLInsecureVideoCodec::openDecoder hints primaries {}, transfer characteristic {}, colourspace {}",
		hints.colorPrimaries, hints.colorTransferCharacteristic, hints.colorSpace);

	if (!handleMasteringMetadata(hints)) {
		CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::openDecoder No Mastering Display data in stream header");
	}

	std::list<EINTERLACEMETHOD> methods;

	getDeinterlacingMethods(methods);
	m_processInfo.UpdateDeinterlacingMethods(methods);

	// setup video codec parameters
	setupVideoCodecParams(m_am_private->gcodec);

	// setup private, codec specific configuration
	std::string codecConfig = getCodecPrivateConfiguration();
	if (!codecConfig.empty()) {
		m_am_private->gcodec.config = const_cast<char*> (codecConfig.c_str());
		m_am_private->gcodec.config_len = (int) codecConfig.size();
	}

	m_am_private->gcodec.param = (void *)((std::uintptr_t)m_am_private->gcodec.param | (m_am_private->video_rotation_degree << 16));

	int ret = m_libamcodec->init(m_am_private->gcodec);
	if (ret != CODEC_ERROR_NONE) {
		CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::openDecoder codec init failed, ret=0x{:x}", -ret);
		return false;
	}

	m_libamcodec->pause();

	m_libamcodec->setControlMode(TRICKMODE_NONE);
	m_libamcodec->setVideoDelayLimitedMs(1000);

	m_libamcodec->setControlAvThreshold(AV_SYNC_THRESH);
	m_libamcodec->setControlSyncThreshold(0);

	// disable tsync, we are playing video disconnected from audio.
	SysfsUtils::SetInt("/sys/class/tsync/enable", 0);

	handle_pre_header_feeding(m_am_private, &m_am_private->am_pkt);

	m_display_rect = CRect(0, 0,
		CDisplaySettings::GetInstance().GetCurrentResolutionInfo().iWidth,
		CDisplaySettings::GetInstance().GetCurrentResolutionInfo().iHeight);

	std::string strScaler;
	SysfsUtils::GetString("/sys/class/ppmgr/ppscaler", strScaler);
	if (strScaler.find("enabled") == std::string::npos) {
		// Scaler not enabled, use screen size
		m_display_rect = CRect(0, 0,
			CDisplaySettings::GetInstance().GetCurrentResolutionInfo().iScreenWidth,
			CDisplaySettings::GetInstance().GetCurrentResolutionInfo().iScreenHeight);
	}

	SysfsUtils::SetInt("/sys/class/video/freerun_mode", 1);

	m_opened = true;

	// vcodec is open, update speed if it was
	// changed before VideoPlayer called OpenDecoder.
	setSpeed(m_speed);

	return true;
}

void AMLInsecureVideoCodec::am_packet_release(am_packet_t *pkt)
{
	if (pkt->buf != nullptr) {
		free(pkt->buf), pkt->buf = nullptr;
	}

	delete pkt->hdr, pkt->hdr = nullptr;
}

void AMLInsecureVideoCodec::closeDecoder()
{
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::closeDecoder");

	// never leave vcodec ff/rw or paused.
	if (m_speed != DVD_PLAYSPEED_NORMAL) {
		//m_dll->codec_resume(&am_private->vcodec);
		m_libamcodec->setControlMode(TRICKMODE_NONE);
	}

	if (m_opened) {
		// close the codec only if it was opened before
		m_libamcodec->close();
		m_opened = false;
	}

	am_packet_release(&m_am_private->am_pkt);
	free(m_am_private->extradata);
	m_am_private->extradata = nullptr;

	// return tsync to default so external apps work
	SysfsUtils::SetInt("/sys/class/tsync/enable", 1);

	/* disable custom master data */
	enableCustomMasterDataDisplay(false);

	setVideoContrast(50);
	setVideoBrightness(50);

	showMainVideo(false);

	closeAmlVideo();
}

void AMLInsecureVideoCodec::setVideoContrast(const int contrast) const
{
  // input contrast range is 0 to 100 with default of 50.
  // output contrast range is -127 to 127 with default of 0.
  int aml_contrast = (127.0f * (contrast - 50.0f)) / 50.0f + 0.5f;
  SysfsUtils::SetInt("/sys/class/video/contrast", aml_contrast);
}

int AMLInsecureVideoCodec::getVideoContrast() const
{
	std::string aml_contrast;

	SysfsUtils::GetString("/sys/class/video/contrast", aml_contrast);
	return std::stoi(aml_contrast) * 50.0f / 127.0f + 50.5f;
}

void AMLInsecureVideoCodec::setVideoBrightness(const int brightness) const
{
  // input brightness range is 0 to 100 with default of 50.
  // output brightness range is -512 to 511 with default of 0.
  // we limit that to -256 to 255 with default of 0.
  int aml_brightness = (255.0f * (brightness - 50.0f)) / 50.0f + 0.5f;
  SysfsUtils::SetInt("/sys/class/video/brightness", aml_brightness);
}

int AMLInsecureVideoCodec::getVideoBrightness() const
{
	std::string aml_brightness;

	SysfsUtils::GetString("/sys/class/video/brightness", aml_brightness);
	return std::stoi(aml_brightness) * 50.0f / 255.0f + 50.5f;
}

bool AMLInsecureVideoCodec::prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts)
{
	// nothing to do here
	return true;
}

int AMLInsecureVideoCodec::set_header_info(am_private_t *para) const
{
	// nothing to do here
	return PLAYER_SUCCESS;
}

int AMLInsecureVideoCodec::check_in_pts(am_private_t *para, am_packet_t *pkt) const
{
	if (para->stream_type == AM_STREAM_ES && UINT64_0 != pkt->avpts && m_libamcodec->checkinPts64(pkt->avpts) != 0) {
		CLog::Log(LOGDEBUG, "ERROR check in pts error!");
		return PLAYER_PTS_ERROR;
	}

	return PLAYER_SUCCESS;
}

int AMLInsecureVideoCodec::write_header(am_private_t *para, am_packet_t *pkt) const
{
	unsigned int write_bytes = 0, len = 0;

	if (pkt->hdr->isEmpty()) {
		return PLAYER_SUCCESS;
	}

	if (m_libamcodec == nullptr || !m_libamcodec->isValid()) {
		CLog::Log(LOGDEBUG, "[write_header]codec null!");
		return PLAYER_EMPTY_P;
	}

	while (1) {
		write_bytes = m_libamcodec->write(pkt->hdr->data + len, pkt->hdr->size - len);
		if (write_bytes < 0 || write_bytes > (pkt->hdr->size - len)) {
			if (-errno != AVERROR(EAGAIN)) {
				CLog::Log(LOGDEBUG, "ERROR:write header failed!");
				return PLAYER_WR_FAILED;
			} else {
				continue;
			}
		} else {
			len += write_bytes;
			if (len == pkt->hdr->size) {
				break;
			}
		}
	}

	return PLAYER_SUCCESS;
}

int AMLInsecureVideoCodec::write_av_packet(am_private_t *para, am_packet_t *pkt) const
{
	int write_bytes = 0, len = 0, ret;
	unsigned char *buf;
	int size;

	// do we need to check in pts or write the header ?
	if (pkt->newflag) {
		if (pkt->isvalid) {
			ret = check_in_pts(para, pkt);
			if (ret != PLAYER_SUCCESS) {
				CLog::Log(LOGDEBUG, "check in pts failed");
				return PLAYER_WR_FAILED;
			}
		}
		if (write_header(para, pkt) == PLAYER_WR_FAILED) {
			CLog::Log(LOGDEBUG, "[{}]write header failed!", __FUNCTION__);
			return PLAYER_WR_FAILED;
		}
		pkt->newflag = 0;
	}

	buf = pkt->data;
	size = pkt->data_size;
	if (size == 0 && pkt->isvalid) {
		pkt->isvalid = 0;
		pkt->data_size = 0;
	}

	while (size > 0 && pkt->isvalid) {
		write_bytes = m_libamcodec->write(buf, size);
		if (write_bytes < 0 || write_bytes > size) {
			CLog::Log(LOGDEBUG, "write codec data failed, write_bytes({}), errno({}), size({})", write_bytes, errno, size);
			if (-errno != AVERROR(EAGAIN)) {
				CLog::Log(LOGDEBUG, "write codec data failed!");
				return PLAYER_WR_FAILED;
			} else {
				// adjust for any data we already wrote into codec.
				// we sleep a bit then exit as we will get called again
				// with the same pkt because pkt->isvalid has not been cleared.
				pkt->data += len;
				pkt->data_size -= len;
				usleep(RW_WAIT_TIME);
				CLog::Log(LOGDEBUG, "Codec buffer full, try after {} ms, len({})", RW_WAIT_TIME / 1000, len);
				return PLAYER_SUCCESS;
			}
		} else {
			// keep track of what we write into codec from this pkt
			// in case we get hit with EAGAIN.
			len += write_bytes;
			if (len == pkt->data_size) {
				pkt->isvalid = 0;
				pkt->data_size = 0;
				break;
			} else if (len < pkt->data_size) {
				buf += write_bytes;
				size -= write_bytes;
			} else {
				// writing more that we should is a failure.
				return PLAYER_WR_FAILED;
			}
		}
	}

	return PLAYER_SUCCESS;
}

bool AMLInsecureVideoCodec::addData(uint8_t *pData, size_t iSize, double dts, double pts, uint8_t subtitlePlane)
{
	if (!m_decInputQueue->isAvailable(iSize)) {
		// decoder input queue is almost full
		return false;
	}

	/* if passing through HDR, ignore contrast and brightness
	 * thus users can view HDR on both HDR and SDR screens without faff */
	int curCscType, hdrProcessMode, hdr10plusProcessMode, hlgProcessMode;
	std::string cur_csc_type;
	SysfsUtils::GetString("/sys/module/am_vecm/parameters/cur_csc_type", cur_csc_type);
	curCscType = std::stoi(cur_csc_type.substr(0, 2));
	SysfsUtils::GetInt("/sys/module/am_vecm/parameters/hdr_process_mode", hdrProcessMode);
	SysfsUtils::GetInt("/sys/module/am_vecm/parameters/hdr10_plus_process_mode", hdr10plusProcessMode);
	SysfsUtils::GetInt("/sys/module/am_vecm/parameters/hlg_process_mode", hlgProcessMode);
	bool isHDR = curCscType == 64 && hdrProcessMode == 0
		&& hdr10plusProcessMode == 0 && hlgProcessMode == 0;

	// video contrast adjustment.
	int contrast = m_processInfo.GetVideoSettings().m_Contrast;
	if (isHDR) {
		contrast = 50;
	}

	m_contrast = getVideoContrast();
	if (contrast != m_contrast) {
		setVideoContrast(contrast);
		m_contrast = contrast;
	}

	// video brightness adjustment.
	int brightness = m_processInfo.GetVideoSettings().m_Brightness;
	if (isHDR) {
		brightness = 50;
	}

	m_brightness = getVideoBrightness();
	if (brightness != m_brightness) {
		setVideoBrightness(brightness);
		m_brightness = brightness;
	}

	// band fix
	SysfsUtils::SetString("/sys/class/amhdmitx/amhdmitx0/debug", "round1");

	// setup packet
	m_am_private->am_pkt.data = pData;
	m_am_private->am_pkt.data_size = iSize;

	m_am_private->am_pkt.newflag = 1;
	m_am_private->am_pkt.isvalid = 1;
	m_am_private->am_pkt.avduration = 0;

	// setup dts/pts
	if (m_hints.ptsinvalid || pts == DVD_NOPTS_VALUE) {
		m_am_private->am_pkt.avpts = UINT64_0;
	} else {
		m_am_private->am_pkt.avpts = pts;
		m_state |= STATE_HASPTS;
	}

	if (dts == DVD_NOPTS_VALUE) {
		m_am_private->am_pkt.avdts = m_am_private->am_pkt.avpts;
	} else {
		m_am_private->am_pkt.avdts = dts;

		// For VC1 AML decoder uses PTS only on I-Frames
		if (m_am_private->am_pkt.avpts == UINT64_0 && (((size_t) m_am_private->gcodec.param) & UNSTABLE_PTS)) {
			m_am_private->am_pkt.avpts = m_am_private->am_pkt.avdts;
		}
	}

	// We use this to determine the fill state if no PTS is given
	if (m_cur_pts == DVD_NOPTS_VALUE) {
		// No PTS given -> use first DTS for AML ptsserver initialization
		if ((m_state & STATE_HASPTS) == 0) {
			m_am_private->am_pkt.avpts = m_am_private->am_pkt.avdts;
		}
	}

	// some formats need header/data tweaks.
	// the actual write occurs once in write_av_packet
	// and is controlled by am_pkt.newflag.
	set_header_info(m_am_private);

	// pass along the frame to secureOSMC
	m_secureOSMC->parseFrame(m_hints.codec_tag, subtitlePlane, m_am_private->am_pkt.avpts * PTS_FREQ / DVD_TIME_BASE, pData, iSize);

	// loop until we write all into codec, am_pkt.isvalid
	// will get set to zero once everything is consumed.
	// PLAYER_SUCCESS means all is ok, not all bytes were written.
	int loop = 0;
	while (m_am_private->am_pkt.isvalid && loop < 100) {
		// abort on any errors.
		if (write_av_packet(m_am_private, &m_am_private->am_pkt) != PLAYER_SUCCESS) {
			break;
		}

		if (m_am_private->am_pkt.isvalid) {
			CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::addData: write_av_packet looping, lvl:{:0.3f}", m_decInputQueue->fillLevel());
		}
		loop++;
	}

	if (loop == 100) {
		// Decoder got stuck; Reset
		reset();
		return false;
	}

	m_decInputQueue->addFrame(iSize);

	// give decoder some time to settle
	std::this_thread::sleep_for(2ms);

	if (iSize > 0) {
		CLog::Log(LOGDEBUG, LOGVIDEO, "AMLInsecureVideoCodec::addData: sz:{} dts_in:{:0.3f} pts_in:{:0.3f} ptsOut:{:0.3f} lvl:{:0.3f}",
			static_cast<unsigned int>(iSize),
			dts / DVD_TIME_BASE,
			pts / DVD_TIME_BASE,
			static_cast<float>(m_cur_pts) / DVD_TIME_BASE,
			m_decInputQueue->fillLevel());
	}

	return true;
}

void AMLInsecureVideoCodec::reset()
{
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::reset");

	if (!m_opened) {
		return;
	}

	// set the system blackout_policy to leave the last frame showing
	int blackout_policy;
	SysfsUtils::GetInt("/sys/class/video/blackout_policy", blackout_policy);
	SysfsUtils::SetInt("/sys/class/video/blackout_policy", 0);

	// restore the speed (some amcodec versions require this)
	if (m_speed != DVD_PLAYSPEED_NORMAL) {
		m_libamcodec->setControlMode(TRICKMODE_NONE);
	}
	m_libamcodec->pause();

	// reset the decoder
	m_libamcodec->reset();
	m_libamcodec->setVideoDelayLimitedMs(1000);

	// re-init our am_pkt
	am_packet_release(&m_am_private->am_pkt);
	am_packet_init(&m_am_private->am_pkt);
	handle_pre_header_feeding(m_am_private, &m_am_private->am_pkt);

	// restore the saved system blackout_policy value
	SysfsUtils::SetInt("/sys/class/video/blackout_policy", blackout_policy);

	// reset some interal vars
	m_cur_pts = DVD_NOPTS_VALUE;
	m_state = 0;
	m_filling = true;
	m_decInputQueue->reset();

	m_input_queue_length = 0;

	setSpeed(m_speed);
}

void AMLInsecureVideoCodec::setDrain(bool drain)
{
	m_drain = drain;
	m_drainRepeatCount = m_drain ? drainRepeatCount : 0;
}

bool AMLInsecureVideoCodec::dequeueBuffer()
{
	v4l2_buffer vbuf = { 0 };
	vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (m_amlVideoFile->IOControl(VIDIOC_DQBUF, &vbuf) < 0) {
		if (errno != EAGAIN) {
			CLog::Log(LOGERROR, "AMLInsecureVideoCodec::dequeueBuffer - VIDIOC_DQBUF failed: {}", strerror(errno));
		}
		return false;
	}

	m_last_pts = m_cur_pts;

	m_cur_pts = static_cast<uint64_t>(vbuf.timestamp.tv_sec) << 32;
	m_cur_pts |= static_cast<uint64_t>(vbuf.timestamp.tv_usec & 0xFFFFFFFF);

	m_bufferIndex = vbuf.index;

	// The interlaced flag was initially set when the decoder was opened. But when the decoder
	// is doing its job, it finds out the truth.
	m_hints.interlaced = (vbuf.field & V4L2_FIELD_INTERLACED) != 0;

	CLog::Log(LOGDEBUG, LOGAVTIMING, "AMLInsecureVideoCodec::dequeueBuffer: pts: {:0.3f} idx: {} interlaced: {}",
			  static_cast<double>(m_cur_pts) / DVD_TIME_BASE, vbuf.index, m_hints.interlaced);

	return true;
}

void AMLInsecureVideoCodec::setPictureStereoMode(VideoPicture *pVideoPicture)
{
	pVideoPicture->stereoMode = m_hints.stereo_mode;

	if (pVideoPicture->stereoMode.empty() || !m_processInfo.GetVideoSettings().m_StereoInvert) {
		// either no stereo mode or it shouldn't be inverted
		return;
	}

	if (pVideoPicture->stereoMode == "left_right") {
		pVideoPicture->stereoMode = "right_left";
	} else if (pVideoPicture->stereoMode == "right_left") {
		pVideoPicture->stereoMode = "left_right";
	} else if (pVideoPicture->stereoMode == "top_bottom") {
		pVideoPicture->stereoMode = "bottom_top";
	} else if (pVideoPicture->stereoMode == "bottom_top") {
		pVideoPicture->stereoMode = "top_bottom";
	} else if (pVideoPicture->stereoMode == "block_lr") {
		pVideoPicture->stereoMode = "block_rl";
	} else if (pVideoPicture->stereoMode == "block_rl") {
		pVideoPicture->stereoMode = "block_lr";
	}
}

CDVDVideoCodec::VCReturn AMLInsecureVideoCodec::getPicture(VideoPicture *pVideoPicture)
{
	if (!m_opened) {
		return CDVDVideoCodec::VC_ERROR;
	}

	if (m_drain) {
		if (dequeueBuffer()) {
			// we're draining, so just drop what we've got
			releaseFrame(m_bufferIndex, true);
		}

		// give decoder some time to settle
		std::this_thread::sleep_for(5ms);

		// m_drainRepeatCount tells us indirectly how long we want to wait until everything's squeezed
		// out of the decoder. Since we give the decoder 5 ms per try, m_drainRepeatCount refers
		// to a time period (m_drainRepeatCount * 5 ms).
		return m_drainRepeatCount-- > 0 ? CDVDVideoCodec::VC_NONE : CDVDVideoCodec::VC_EOF;
	}

	unsigned int fps = UNIT_FREQ / m_am_private->video_rate;
	float level = m_decInputQueue->fillLevel();
	unsigned waitingFrameCount = m_decInputQueue->frameCount();

	if (waitingFrameCount < 5 && level < maxDecoderInputLevel) {
		return CDVDVideoCodec::VC_BUFFER;
	}

	// give decoder some time to settle
	std::this_thread::sleep_for(7ms);

	m_filling = (waitingFrameCount < fps && level < maxDecoderInputLevel) || m_libamcodec->isVCodecBuffering();

	if (!dequeueBuffer()) {
		return m_filling ? CDVDVideoCodec::VC_BUFFER : CDVDVideoCodec::VC_NONE;
	}

	pVideoPicture->iFlags = 0;

	double duration = static_cast<double>((m_am_private->video_rate * DVD_TIME_BASE) / UNIT_FREQ);

	float dec_vr = (float) m_am_private->video_rate;
	float vr = (float) getVideoRate(m_hints);

	if (!m_hints.interlaced && MathUtils::FloatEquals(vr / dec_vr, 2.f, 0.01f)) {
		// the kernel refresh rate was set to double video rate
		duration *= 2.0;
	}

	if (m_last_pts == DVD_NOPTS_VALUE) {
		pVideoPicture->iDuration = duration;
	} else if (m_cur_pts < m_last_pts) {
		pVideoPicture->iDuration = duration;
		m_cur_pts = m_last_pts + duration;
	} else {
		pVideoPicture->iDuration = static_cast<double>(m_cur_pts - m_last_pts);

		double duration_ratio = pVideoPicture->iDuration / duration;

		// pts order not correct (sometimes, the pts_server in the kernel returns wrong
		// pts values => try to compensate). If the difference is too big, then we assume
		// there's a leap in the stream's pts values
		if (duration_ratio < 0.2 || (duration_ratio > 1.5 && duration_ratio < 4.0)) {
			pVideoPicture->iDuration = duration;
			m_cur_pts = m_last_pts + duration;
		}
	}

	pVideoPicture->dts = DVD_NOPTS_VALUE;
	pVideoPicture->pts = static_cast<double>(m_cur_pts);

	CLog::Log(LOGDEBUG, LOGVIDEO, "AMLInsecureVideoCodec::getPicture: index: {}, pts: {:0.3f}, dur:{:0.3f}ms",
		m_bufferIndex, pVideoPicture->pts / DVD_TIME_BASE, pVideoPicture->iDuration / 1000.);

	setPictureStereoMode(pVideoPicture);

	m_input_queue_length++;

	return CDVDVideoCodec::VC_PICTURE;
}

int AMLInsecureVideoCodec::getOMXPts() const
{
	return static_cast<int>(m_cur_pts);
}

uint32_t AMLInsecureVideoCodec::getBufferIndex() const
{
	return m_bufferIndex;
}

int AMLInsecureVideoCodec::getAmlDuration() const
{
	return m_am_private ? (m_am_private->video_rate * PTS_FREQ) / UNIT_FREQ : 0;
}

int AMLInsecureVideoCodec::releaseFrame(const uint32_t index, bool drop)
{
	if (!m_amlVideoFile) {
		return 0;
	}

	v4l2_buffer vbuf = { 0 };
	vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vbuf.index = index;

	if (drop) {
		vbuf.flags |= V4L2_BUF_FLAG_DONE;
	}

	CLog::Log(LOGDEBUG, LOGVIDEO, "AMLInsecureVideoCodec::releaseFrame idx:{}, drop:{}", index, static_cast<int>(drop));

	int ret;
	if ((ret = m_amlVideoFile->IOControl(VIDIOC_QBUF, &vbuf)) < 0) {
		CLog::Log(LOGERROR, "AMLInsecureVideoCodec::releaseFrame - VIDIOC_QBUF failed: {}", strerror(errno));
	}

	m_input_queue_length--;

	return ret;
}

unsigned int AMLInsecureVideoCodec::getDecoderVideoRate() const
{
	if (m_speed != DVD_PLAYSPEED_NORMAL || !m_libamcodec->isValid()) {
		return 0;
	}

	struct vdec_info vi;

	m_libamcodec->getVdecInfo(vi);

	if (vi.frame_dur == 0) {
		return 0;
	}

	if (m_hints.interlaced && vi.frame_dur >= 3200) {
		return vi.frame_dur / 2;
	}

	return vi.frame_dur;
}

void AMLInsecureVideoCodec::setFramepackingResolution(const int width, const int height, const int blanking) const
{
  SysfsUtils::SetInt("/sys/module/amvideo/parameters/framepacking_width", width);
  SysfsUtils::SetInt("/sys/module/amvideo/parameters/framepacking_height", height * 2 + blanking);
  SysfsUtils::SetInt("/sys/module/amvideo/parameters/framepacking_blank", blanking);
}

void AMLInsecureVideoCodec::setVideoRect(const CRect &SrcRect, const CRect &DestRect)
{
	// this routine gets called every video frame
	// and is in the context of the renderer thread so
	// do not do anything stupid here.
	bool update = false;

	// video zoom adjustment.
	float zoom = m_processInfo.GetVideoSettings().m_CustomZoomAmount;
	if ((int) (zoom * 1000) != (int) (m_zoom * 1000)) {
		m_zoom = zoom;
	}

	// video rate adjustment.
	unsigned int video_rate = getDecoderVideoRate();
	if (video_rate > 0 && video_rate != m_am_private->video_rate) {
		CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::setVideoRect: decoder fps has changed, video_rate adjusted from {} to {}", m_am_private->video_rate, video_rate);
		m_am_private->video_rate = video_rate;
	}

	// video view mode
	int view_mode = m_processInfo.GetVideoSettings().m_ViewMode;
	if (m_view_mode != view_mode) {
		m_view_mode = view_mode;
		update = true;
	}

	// video orientation
	int orientation = m_processInfo.GetVideoSettings().m_Orientation;
	if (m_orientation != orientation) {
		m_orientation = orientation;
		update = true;
	}

	// GUI stereo mode/view.
	RENDER_STEREO_MODE guiStereoMode = CServiceBroker::GetGUI()->GetStereoscopicsManager().GetStereoMode();
	if (m_guiStereoMode != guiStereoMode) {
		m_guiStereoMode = guiStereoMode;
		update = true;
	}
	RENDER_STEREO_VIEW guiStereoView = CServiceBroker::GetWinSystem()->GetGfxContext().GetStereoView();
	if (m_guiStereoView != guiStereoView) {
		// left/right/top/bottom eye,
		// this might change every other frame.
		// we do not care but just track it.
		m_guiStereoView = guiStereoView;
	}

	// dest_rect
	CRect dst_rect = DestRect;
	if (m_dst_rect != dst_rect) {
		m_dst_rect = dst_rect;
		update = true;
	}

	RESOLUTION video_res = CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution();
	if (m_video_res != video_res) {
		m_video_res = video_res;
		update = true;
	}

	std::string videoStereoMode = m_processInfo.GetVideoStereoMode();
	if (m_video_stereo_mode != videoStereoMode) {
		m_video_stereo_mode = videoStereoMode;
		update = true;
	}

	if (!update) {
		// mainvideo 'should' be showing already if we get here, make sure.
		showMainVideo(true);
		return;
	}

	CRect gui, display;

	const RESOLUTION_INFO &video_res_info = CDisplaySettings::GetInstance().GetResolutionInfo(video_res);
	display = m_display_rect = CRect(0, 0, video_res_info.iScreenWidth, video_res_info.iScreenHeight);
	gui = CRect(0, 0, video_res_info.iWidth, video_res_info.iHeight);

	if (gui != display) {
		float xscale = display.Width() / gui.Width();
		float yscale = display.Height() / gui.Height();
		if (m_guiStereoMode == RENDER_STEREO_MODE_SPLIT_VERTICAL)
			xscale /= 2.0;
		else if (m_guiStereoMode == RENDER_STEREO_MODE_SPLIT_HORIZONTAL)
			yscale /= 2.0;
		dst_rect.x1 *= xscale;
		dst_rect.x2 *= xscale;
		dst_rect.y1 *= yscale;
		dst_rect.y2 *= yscale;
	}

	if (m_guiStereoMode == RENDER_STEREO_MODE_MONO) {
		if (videoStereoMode == "left_right" || videoStereoMode == "right_left")
			dst_rect.x2 *= 2.0;
		else if (videoStereoMode == "top_bottom" || videoStereoMode == "bottom_top")
			dst_rect.y2 *= 2.0;
	} else if (m_guiStereoMode == RENDER_STEREO_MODE_SPLIT_VERTICAL) {
		dst_rect.x2 *= 2.0;
	} else if (m_guiStereoMode == RENDER_STEREO_MODE_SPLIT_HORIZONTAL) {
		dst_rect.y2 *= 2.0;
	} else if (m_guiStereoMode == RENDER_STEREO_MODE_HARDWAREBASED) {
		// 3D frame packed output: get the screen height from the graphic context
		// (will work in fullscreen mode only)
		RESOLUTION_INFO info = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo();
		dst_rect.x2 = info.iWidth;
		dst_rect.y2 = info.iHeight * 2 + info.iBlanking;
		setFramepackingResolution(info.iWidth, info.iHeight, info.iBlanking);
	}

	setVideoMode(videoStereoMode, m_guiStereoMode);

	std::string s_dst_rect = StringUtils::Format("{},{},{},{}", (int) dst_rect.x1, (int) dst_rect.y1, (int) dst_rect.Width(), (int) dst_rect.Height());
	std::string s_m_dst_rect = StringUtils::Format("{},{},{},{}", (int) m_dst_rect.x1, (int) m_dst_rect.y1, (int) m_dst_rect.Width(),
		(int) m_dst_rect.Height());
	std::string s_display = StringUtils::Format("{},{},{},{}", (int) m_display_rect.x1, (int) m_display_rect.y1, (int) m_display_rect.Width(),
		(int) m_display_rect.Height());
	std::string s_gui = StringUtils::Format("{},{},{},{}", (int) gui.x1, (int) gui.y1, (int) gui.Width(), (int) gui.Height());
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::setVideoRect:display({})", s_display);
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::setVideoRect:gui({})", s_gui);
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::setVideoRect:m_dst_rect({})", s_m_dst_rect);
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::setVideoRect:dst_rect({})", s_dst_rect);
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::setVideoRect:m_guiStereoMode({})", m_guiStereoMode);
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::setVideoRect:m_guiStereoView({})", m_guiStereoView);
	CLog::Log(LOGDEBUG, "AMLInsecureVideoCodec::setVideoRect video rotation {} user rotation {}", m_am_private->video_rotation_degree * 90, m_orientation * 90);

	// goofy 0/1 based difference in aml axis coordinates.
	// fix them.
	dst_rect.x2--;
	dst_rect.y2--;

	char video_axis[256] = { };
	sprintf(video_axis, "%d %d %d %d", (int) dst_rect.x1, (int) dst_rect.y1, (int) dst_rect.x2, (int) dst_rect.y2);

	int screen_mode = CDisplaySettings::GetInstance().IsNonLinearStretched() ? 4 : 1;

	SysfsUtils::SetString("/sys/class/video/axis", video_axis);
	SysfsUtils::SetInt("/sys/class/video/screen_mode", screen_mode);
	SysfsUtils::SetInt("/sys/class/ppmgr/orientation", m_orientation);

	// we only get called once gui has changed to something
	// that would show video playback, so show it.
	showMainVideo(true);
}

void AMLInsecureVideoCodec::setVideoMode(std::string videoInputMode, RENDER_STEREO_MODE videoOutputMode)
{
	video_input_mode vimode = VIDEO_INPUT_MODE_2D;
	video_output_mode vomode = VIDEO_OUTPUT_MODE_2D;
	bool leftEyeFirst = true;

	if (videoInputMode == "left_right") {
		vimode = VIDEO_INPUT_MODE_HSBS;
	} else if (videoInputMode == "right_left") {
		vimode = VIDEO_INPUT_MODE_HSBS;
		leftEyeFirst = false;
	} else if (videoInputMode == "top_bottom") {
		vimode = VIDEO_INPUT_MODE_HTAB;
	} else if (videoInputMode == "bottom_top") {
		vimode = VIDEO_INPUT_MODE_HTAB;
		leftEyeFirst = false;
	} else if (videoInputMode == "block_lr") {
		vimode = VIDEO_INPUT_MODE_MVC;
	} else if (videoInputMode == "block_rl") {
		vimode = VIDEO_INPUT_MODE_MVC;
		leftEyeFirst = false;
	}

	// handle Full-SBS/Full-TAB
	if (vimode == VIDEO_INPUT_MODE_HSBS && m_hints.width == 3840) {
		vimode = VIDEO_INPUT_MODE_FSBS;
	} else if (vimode == VIDEO_INPUT_MODE_HTAB && m_hints.height > 1080) {
		vimode = VIDEO_INPUT_MODE_FTAB;
	}

	if (vimode != VIDEO_INPUT_MODE_2D) {
		switch (videoOutputMode) {
			case RENDER_STEREO_MODE_SPLIT_VERTICAL:
				vomode = VIDEO_OUTPUT_MODE_HSBS;
				break;

			case RENDER_STEREO_MODE_SPLIT_HORIZONTAL:
				vomode = VIDEO_OUTPUT_MODE_HTAB;
				break;

			case RENDER_STEREO_MODE_HARDWAREBASED:
				vomode = VIDEO_OUTPUT_MODE_FP;
				break;

			default:
				break;
		}
	}

	m_libamcodec->setVideoMode(vimode, vomode, leftEyeFirst);
}

double AMLInsecureVideoCodec::getAspectRatio() const
{
	return m_opened ? m_hints.aspect : 0;
}
