/*
 *	(c) 2024 OSMC
 *
 *	Inspired by work from Anton Fedchin <anightik@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDInputStreamBlurayExtension.h"

#include <sys/types.h>

#include "cores/FFmpeg.h"
#include "cores/VideoPlayer/DVDDemuxers/DVDDemuxUtils.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "utils/log.h"

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavutil/opt.h>
}

const int MVC_SEEK_TIME_WINDOW = 7500;		// experimental value depends on seeking accurate

static int clip_file_read(void *h, uint8_t *buf, int size);
static int64_t clip_file_seek(void *h, int64_t pos, int whence);


CDVDInputStreamBlurayExtension::CDVDInputStreamBlurayExtension(BLURAY *bd, int subPath)
	: m_bd(bd), m_subPath(subPath), m_clip(-1), m_file(nullptr), m_formatContext(nullptr), m_ioContext(nullptr),
	  m_avStreamIndex(-1), m_starttime(AV_NOPTS_VALUE)
{
}

CDVDInputStreamBlurayExtension::~CDVDInputStreamBlurayExtension()
{
	dispose();
}

void CDVDInputStreamBlurayExtension::setStartTime(int64_t ms)
{
	m_starttime = ms;
}

bool CDVDInputStreamBlurayExtension::selectClip(int clip)
{
	if (m_clip == clip) {
		return true;
	}

	dispose();

	return openClip(clip);
}

bool CDVDInputStreamBlurayExtension::seekTime(double ms, bool backwards) const
{
	if (m_clip == -1 || !m_file) {
		return false;
	}

	const AVRational timeBase = m_formatContext->streams[m_avStreamIndex]->time_base;
	int64_t seek_pts = av_rescale(DVD_MSEC_TO_TIME(ms), timeBase.den, static_cast<int64_t>(timeBase.num) * AV_TIME_BASE);

	if (m_formatContext->streams[m_avStreamIndex]->start_time != AV_NOPTS_VALUE) {
		seek_pts += m_formatContext->streams[m_avStreamIndex]->start_time;
	}

	if (seek_pts < MVC_SEEK_TIME_WINDOW) {
		seek_pts = 0;
	} else {
		seek_pts -= MVC_SEEK_TIME_WINDOW;
	}

	av_seek_frame(m_formatContext, m_avStreamIndex, seek_pts, backwards ? AVSEEK_FLAG_BACKWARD : 0);

	return true;
}

DemuxPacket *CDVDInputStreamBlurayExtension::read()
{
	if (m_clip == -1 || !m_file) {
		return nullptr;
	}

	AVPacket *extPacket = av_packet_alloc();

	while (true) {
		const int ret = av_read_frame(m_formatContext, extPacket);

		if (ret == AVERROR(EINTR) || ret == AVERROR(EAGAIN)) {
			continue;
		}

		if (ret == AVERROR_EOF) {
			break;
		}

		if (extPacket->size <= 0 || extPacket->stream_index != m_avStreamIndex) {
			av_packet_unref(extPacket);
			continue;
		}

		AVStream *stream = m_formatContext->streams[extPacket->stream_index];
		DemuxPacket *newPkt = CDVDDemuxUtils::AllocateDemuxPacket(extPacket->size);

		if (extPacket->data) {
			memcpy(newPkt->pData, extPacket->data, extPacket->size);
		}

		newPkt->iSize = extPacket->size;
		newPkt->dts = convertTimestamp(extPacket->dts, stream->time_base.den, stream->time_base.num);
		newPkt->pts = convertTimestamp(extPacket->pts, stream->time_base.den, stream->time_base.num);
		newPkt->iStreamId = stream->id;

		av_packet_unref(extPacket);

		return newPkt;
	}

	return nullptr;
}

bool CDVDInputStreamBlurayExtension::openClip(int clip)
{
	MPLS_CLIP *extClip = findExtensionClip(clip);
	if (!extClip) {
		return false;
	}

	std::string stream = std::string(extClip->clip_id) + ".m2ts";
	if (!openClip(stream)) {
		dispose();
		return false;
	}

	m_clip = clip;

	return true;
}

bool CDVDInputStreamBlurayExtension::openClip(std::string &stream)
{
	const int bufferSize = 6144;

	m_file = bd_clip_open(m_bd, stream.c_str());
	if (!m_file) {
		return false;
	}

	unsigned char *buffer = (unsigned char*)av_malloc(bufferSize);

	m_ioContext = avio_alloc_context(buffer, bufferSize, 0, m_file, clip_file_read, nullptr, clip_file_seek);

	m_formatContext = avformat_alloc_context();
	m_formatContext->pb = m_ioContext;

	const AVInputFormat *format = av_find_input_format("mpegts");

	int ret = avformat_open_input(&m_formatContext, stream.c_str(), format, nullptr);
	if (ret < 0) {
		CLog::LogF(LOGDEBUG, "opening clip failed (ffmpeg returns: {})", ret);
		return false;
	}

	av_opt_set_int(m_formatContext, "analyzeduration", 500000, 0);
	av_opt_set_int(m_formatContext, "correct_ts_overflow", 0, 0);

	// Find the streams, it always returns -1 so just ignore it
	avformat_find_stream_info(m_formatContext, nullptr);

	// print some extra information
	av_dump_format(m_formatContext, 0, stream.c_str(), 0);

	// Find and select our extension stream
	CLog::LogF(LOGDEBUG, "extension clip has {} streams", m_formatContext->nb_streams);
	for (unsigned i = 0; i < m_formatContext->nb_streams; i++) {
		if (m_formatContext->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264_MVC
				&& m_formatContext->streams[i]->codecpar->extradata_size > 0) {
			m_avStreamIndex = i;
		} else {
			m_formatContext->streams[i]->discard = AVDISCARD_ALL;
		}
	}

	if (m_avStreamIndex < 0) {
		CLog::LogF(LOGDEBUG, "MVC extension stream not found");
		return false;
	}

	return true;
}

MPLS_CLIP *CDVDInputStreamBlurayExtension::findExtensionClip(int clip)
{
	mpls_pl *pl = bd_get_title_mpls(m_bd);
	if (!pl) {
		return nullptr;
	}

	for (size_t i = 0; i < pl->list_count; i++) {
		if (clip == std::stoi(pl->play_item[i].clip->clip_id)) {
			return pl->ext_sub_path[m_subPath].sub_play_item[i].clip;
		}
	}

	return nullptr;
}

void CDVDInputStreamBlurayExtension::dispose()
{
	if (m_file) {
		bd_clip_close(m_file);
		m_file = nullptr;
	}

	if (m_formatContext) {
		avformat_close_input(&m_formatContext);
		m_formatContext = nullptr;
	}

	if (m_ioContext) {
		av_free(m_ioContext->buffer);
		av_free(m_ioContext);
		m_ioContext = nullptr;
	}

	m_avStreamIndex = -1;
	m_clip = -1;
}

double CDVDInputStreamBlurayExtension::convertTimestamp(int64_t pts, int den, int num) const
{
	if (pts == (int64_t) AV_NOPTS_VALUE) {
		return DVD_NOPTS_VALUE;
	}

	// we don't care for having a completely exact timestamp anyway
	double timestamp = (double) pts * num / den;
	double starttime = 0.0f;

	if (m_starttime != AV_NOPTS_VALUE) {
		starttime = static_cast<double>(m_starttime) / AV_TIME_BASE;
	}

	if (timestamp > starttime) {
		timestamp -= starttime;
	} else if (timestamp + 0.5f > starttime) {
		// allow for largest possible difference in pts and dts for a single packet
	    timestamp = 0;
	}

	return timestamp * DVD_TIME_BASE;
}

FFmpegExtraData CDVDInputStreamBlurayExtension::getExtraData() const
{
	AVStream *mvcStream = m_formatContext->streams[m_avStreamIndex];

	return FFmpegExtraData(mvcStream->codecpar->extradata, mvcStream->codecpar->extradata_size);
}

static int clip_file_read(void *h, uint8_t *buf, int size)
{
	int res = (int) bd_clip_read(reinterpret_cast<struct bd_file_s*>(h), buf, size);

	return res == 0 ? AVERROR_EOF : res;
}

static int64_t clip_file_seek(void *h, int64_t pos, int whence)
{
	if (whence == AVSEEK_SIZE) {
		return bd_clip_size(reinterpret_cast<struct bd_file_s*>(h));
	}

	const int64_t clip_block_pos = (pos / 6144) * 6144;

	return bd_clip_seek(reinterpret_cast<struct bd_file_s*>(h), clip_block_pos, whence & ~AVSEEK_FORCE);
}
