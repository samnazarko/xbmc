/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <stdint.h>

extern "C" {
	#include <amcodec/codec.h>
}

#include "namespace.h"

typedef enum {
	VIDEO_MODE_3D_MVC_FP = 1,
	VIDEO_MODE_3D_MVC_SBS = 2,
	VIDEO_MODE_3D_MVC_HTAB = 3,
	VIDEO_MODE_3D_MVC_MONO = 4
} video_mode_3d_t;

typedef struct {
	bool noblock;
	int video_pid;
	int video_type;
	enum FRAME_BASE_VIDEO_PATH video_path;
	stream_type_t stream_type;
	decoder_type_t decoder_type;
	display_mode_type_t display_mode;
	unsigned int format;
	unsigned int width;
	unsigned int height;
	unsigned int rate;
	unsigned int extra;
	unsigned int status;
	unsigned int ratio;
	unsigned long long ratio64;
	void *param;
} aml_generic_param;

class amlogic::LibAmcodec
{
private:
	DllLibAmCodec	*m_dll;
	codec_para_t	*m_codec;

public:
	LibAmcodec();
	virtual ~LibAmcodec();

	bool inline isValid() const { return m_dll != nullptr; }

	int init(const aml_generic_param &gparam);
	int close();

	int pause();
	int reset();
	int write(unsigned char *buf, unsigned int size);
	int poll(unsigned int timeoutMs);

	int getVbufState(struct buf_status &bs) const;
	int getVdecState(struct vdec_status &vs) const;
	bool isVCodecBuffering() const;

	int checkinPts64(uint64_t pts64);

	void setLogCallback(void (*logf)(const char *, ...));
	int setControlMode(unsigned int mode);
	int setControlAvThreshold(unsigned int threshold);
	int setControlSyncThreshold(unsigned int threshold);
	int setVideoDelayLimitedMs(int delay);
	int set3dVideoMode(video_mode_3d_t mode);
	void setNoBlockMode(bool noblock);
	void setStreamType(stream_type_t type);
};
