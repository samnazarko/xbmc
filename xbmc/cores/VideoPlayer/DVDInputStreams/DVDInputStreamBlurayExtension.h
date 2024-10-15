/*
 *	(c) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <string>

extern "C" {
	#include <libbluray/bluray.h>
	#include <libbluray/mpls_data.h>
};

typedef struct AVFormatContext AVFormatContext;

class DemuxPacket;
class FFmpegExtraData;

class CDVDInputStreamBlurayExtension
{
private:
	BLURAY	*m_bd;
	int		 m_subPath;
	int		 m_clip;

	struct bd_file_s *m_file;

	AVFormatContext		*m_formatContext;
	struct AVIOContext	*m_ioContext;
	int					 m_avStreamIndex;
	int64_t				 m_starttime;

public:
	CDVDInputStreamBlurayExtension(BLURAY *bd, int subPath);
	virtual ~CDVDInputStreamBlurayExtension();

	virtual bool selectClip(int clip);
	virtual void setStartTime(int64_t ms);
	virtual bool seekTime(double ms, bool backwards) const;
	virtual FFmpegExtraData getExtraData() const;

	virtual DemuxPacket *read();

private:
	bool openClip(int clip);
	bool openClip(std::string &stream);
	void dispose();

	MPLS_CLIP *findExtensionClip(int clip);

	double convertTimestamp(int64_t pts, int den, int num) const;
};
