/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "LibAmcodec.h"

extern "C" {
	#include <libavcodec/packet.h>
}

typedef enum {
    AM_STREAM_UNKNOWN = 0,
    AM_STREAM_TS,
    AM_STREAM_PS,
    AM_STREAM_ES,
    AM_STREAM_RM,
    AM_STREAM_AUDIO,
    AM_STREAM_VIDEO,
} pstream_type;

struct hdr_buf
{
private:
	unsigned m_realSize;

public:
	hdr_buf() : m_realSize(0), data(nullptr), size(0) {}
	~hdr_buf() {
		if (data) {
			delete[] data, data = nullptr;
			m_realSize = size = 0;
		}
	}

	void alloc(unsigned newSize) {
		if (newSize > m_realSize) {
			if (data) {
				delete[] data;
			}
			data = new unsigned char[newSize];
			m_realSize = size = newSize;
		} else {
			size = newSize;
		}

		size = 0;
	}

	void free() {
		size = 0;
	}

	bool isEmpty() {
		return size == 0 || data == nullptr;
	}

    unsigned char *data;
    unsigned size;
};

typedef struct am_packet {
    AVPacket      avpkt;
    uint64_t      avpts;
    uint64_t      avdts;
    int           avduration;
    int           isvalid;
    int           newflag;
    uint64_t      lastpts;
    unsigned char *data;
    unsigned char *buf;
    int           data_size;
    int           buf_size;
    hdr_buf       *hdr;
} am_packet_t;

typedef struct am_private_t
{
	am_packet_t am_pkt;
	aml_generic_param gcodec;

	pstream_type stream_type;
	int check_first_pts;

	vformat_t video_format;
	int video_pid;
	unsigned int video_codec_id;
	unsigned int video_codec_tag;
	vdec_type_t video_codec_type;
	unsigned int video_width;
	unsigned int video_height;
	unsigned int video_ratio;
	unsigned int video_ratio64;
	unsigned int video_rate;
	unsigned int video_rotation_degree;
	unsigned int extrasize;
	uint8_t *extradata;
} am_private_t;
