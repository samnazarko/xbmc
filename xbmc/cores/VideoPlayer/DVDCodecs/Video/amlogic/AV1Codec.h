/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AMLInsecureVideoCodec.h"

namespace amlogic {
	struct read_bit_buffer;
	struct ObuHeader;

	struct DataBuffer
	{
		DataBuffer() : data(nullptr), size(0) {}
		DataBuffer(const uint8_t *d, size_t s) : data(d), size(s) {}

		const uint8_t *data;
		size_t size;
	};
}

class amlogic::AV1Codec : public amlogic::AMLInsecureVideoCodec
{
private:
	DataBuffer m_data_buffer;

	int valid_obu_type(int obu_type) const;
	int uleb_decode(const uint8_t *buffer, size_t available, uint64_t *value, size_t *length) const;
	int rb_read_bit(struct read_bit_buffer *rb) const;
	int rb_read_literal(struct read_bit_buffer *rb, int bits) const;
	int read_obu_size(const uint8_t *data, size_t bytes_available, size_t *const obu_size, size_t *const length_field_size) const;
	int read_obu_header(struct read_bit_buffer *rb, int is_annexb, struct ObuHeader *header) const;
	int read_obu_header_and_size(const uint8_t *data, size_t bytes_available, int is_annexb,
		struct ObuHeader *obu_header, size_t *const payload_size, size_t *const bytes_read) const;
	int av1_parser_frame(int is_annexb, uint8_t *data, const uint8_t *data_end,
			uint8_t *dst_data, uint32_t *frame_len, uint8_t *meta_buf, uint32_t *meta_len) const;
	int av1_fixup_frame(DataBuffer &db_in);

protected:
	virtual vformat_t getVideoFormat(const CDVDStreamInfo &hints) const;
	virtual vdec_type_t getVideoCodecType(const CDVDStreamInfo &hints) const;
	virtual bool handleMasteringMetadata(const CDVDStreamInfo &hints) const;
	virtual void setupVideoCodecParams(aml_generic_param &params) const;

public:
	AV1Codec(CProcessInfo &processInfo);
	virtual ~AV1Codec();

	virtual std::string getFormatName() const;

	virtual bool prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts);

};
