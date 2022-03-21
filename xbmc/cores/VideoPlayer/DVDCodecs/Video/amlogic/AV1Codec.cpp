/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AV1Codec.h"

#include "AMLCodecList.h"
#include "AMLVideoCodecInfo.h"
#include "AMLVideoCodecConstants.h"

#include "utils/log.h"
#include "utils/AMLUtils.h"
#include "utils/log.h"
#include "settings/Settings.h"
#include "ServiceBroker.h"
#include "settings/SettingsComponent.h"

using namespace amlogic;

// This code below is partly taken from the Amlogic vdec_av1_if.c kernel module.

enum OBU_TYPE {
	OBU_SEQUENCE_HEADER = 1,
	OBU_TEMPORAL_DELIMITER = 2,
	OBU_FRAME_HEADER = 3,
	OBU_TILE_GROUP = 4,
	OBU_METADATA = 5,
	OBU_FRAME = 6,
	OBU_REDUNDANT_FRAME_HEADER = 7,
	OBU_TILE_LIST = 8,
	OBU_PADDING = 15,
};

struct amlogic::ObuHeader {
	size_t size;	// Size (1 or 2 bytes) of the OBU header (including the
					// optional OBU extension header) in the bitstream.
	enum OBU_TYPE type;
	int has_size_field;
	int has_extension;
	// The following fields come from the OBU extension header and therefore are
	// only used if has_extension is true.
	int temporal_layer_id;
	int spatial_layer_id;
};

enum OBU_METADATA_TYPE {
	OBU_METADATA_TYPE_RESERVED_0 = 0,
	OBU_METADATA_TYPE_HDR_CLL = 1,
	OBU_METADATA_TYPE_HDR_MDCV = 2,
	OBU_METADATA_TYPE_SCALABILITY = 3,
	OBU_METADATA_TYPE_ITUT_T35 = 4,
	OBU_METADATA_TYPE_TIMECODE = 5,
};

struct amlogic::read_bit_buffer {
	const uint8_t *bit_buffer;
	const uint8_t *bit_buffer_end;
	uint32_t bit_offset;
};

static const size_t kMaximumLeb128Size = 8;
static const uint8_t kLeb128ByteMask = 0x7f;

class AV1CodecInfo : public AMLVideoCodecInfo
{
public:
	AV1CodecInfo() {
		AMLCodecList::getInstance().registerAmlVideoCodec(this);
	}

	std::string getCodecName() const { return "AV1"; }
	bool isSecure() const { return false; }
	bool canPlay(const CDVDStreamInfo &hints) const {
		if (hints.codec != AV_CODEC_ID_AV1) {
			return false;
		}

		if (!aml_support_av1() || CServiceBroker::GetSettingsComponent()->GetSettings()->GetInt(CSettings::SETTING_VIDEOPLAYER_USEAMCODECAV1)) {
			// AV1 playback is either not supported or intentionally deactivated
			return false;
		}

		return true;
	}

	AMLVideoCodec *createCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints) {
		return new AV1Codec(processInfo);
	}

} av1CodecInfo;

AV1Codec::AV1Codec(CProcessInfo &processInfo)
	: AMLInsecureVideoCodec(processInfo)
{
}

AV1Codec::~AV1Codec()
{
	if (m_data_buffer.data != nullptr) {
		m_data_buffer.size = 0;
		delete[] m_data_buffer.data, m_data_buffer.data = nullptr;
	}
}

std::string AV1Codec::getFormatName() const
{
	return "am-av1";
}

vformat_t AV1Codec::getVideoFormat(const CDVDStreamInfo &hints) const
{
	return VFORMAT_AV1;
}

vdec_type_t AV1Codec::getVideoCodecType(const CDVDStreamInfo &hints) const
{
	return VIDEO_DEC_FORMAT_AV1;
}

void AV1Codec::setupVideoCodecParams(aml_generic_param &params) const
{
	AMLInsecureVideoCodec::setupVideoCodecParams(params);

	params.format = VIDEO_DEC_FORMAT_AV1;
	params.decoder_type = DECODER_TYPE_FRAME_MODE;
	params.param = (void*) EXTERNAL_PTS;
	if (m_hints.ptsinvalid) {
		params.param = (void*) (EXTERNAL_PTS | SYNC_OUTSIDE);
	}
}

int AV1Codec::av1_fixup_frame(DataBuffer &db_in)
{
  uint8_t meta_buffer[1024] = {0};
  uint32_t meta_size = 0;
  uint32_t length = 0;

  uint8_t *src = (uint8_t *) db_in.data;
  uint32_t size = db_in.size;

  if (m_data_buffer.size < size + 0x1000) {
	  if (m_data_buffer.data != nullptr) {
		  delete[] m_data_buffer.data;
	  }
	  m_data_buffer.data = new uint8_t[size + 0x1000];
	  m_data_buffer.size = size + 0x1000;
  }

  uint8_t *data = (uint8_t *) m_data_buffer.data;

  av1_parser_frame(0, src, src + size, data, &length, meta_buffer, &meta_size);

  db_in.data = m_data_buffer.data;
  db_in.size = length;

  return PLAYER_SUCCESS;
}

bool AV1Codec::prepareFrame(CDVDStreamInfo &hints, uint8_t *&data, size_t &size, double dts, double pts)
{
	DataBuffer db(data, size);

	av1_fixup_frame(db);

	data = (uint8_t*) db.data;
	size = db.size;

	return true;
}

int AV1Codec::valid_obu_type(int obu_type) const
{
	int valid_type = 0;

	switch (obu_type) {
		case OBU_SEQUENCE_HEADER:
		case OBU_TEMPORAL_DELIMITER:
		case OBU_FRAME_HEADER:
		case OBU_TILE_GROUP:
		case OBU_METADATA:
		case OBU_FRAME:
		case OBU_REDUNDANT_FRAME_HEADER:
		case OBU_TILE_LIST:
		case OBU_PADDING:
			valid_type = 1;
			break;
		default:
			break;
	}

	return valid_type;
}

int AV1Codec::uleb_decode(const uint8_t *buffer, size_t available, uint64_t *value, size_t *length) const
{
	size_t i;

	if (buffer && value) {
		*value = 0;

		for (i = 0; i < kMaximumLeb128Size && i < available; ++i) {
			const uint8_t decoded_byte = *(buffer + i) & kLeb128ByteMask;

			*value |= ((uint64_t)decoded_byte) << (i * 7);
			if ((*(buffer + i) >> 7) == 0) {
				if (length) {
					*length = i + 1;
				}

				// Fail on values larger than 32-bits to ensure consistent behavior on
				// 32 and 64 bit targets: value is typically used to determine buffer
				// allocation size.
				if (*value > ULONG_MAX)
					return -1;

				return 0;
			}
		}
	}

	// If we get here, either the buffer/value pointers were invalid,
	// or we ran over the available space
	return -1;
}

int AV1Codec::rb_read_bit(struct read_bit_buffer *rb) const
{
	const uint32_t off = rb->bit_offset;
	const uint32_t p = off >> 3;
	const int q = 7 - (int)(off & 0x7);

	if (rb->bit_buffer + p < rb->bit_buffer_end) {
		const int bit = (rb->bit_buffer[p] >> q) & 1;

		rb->bit_offset = off + 1;
		return bit;
	} else {
		return 0;
	}
}

int AV1Codec::rb_read_literal(struct read_bit_buffer *rb, int bits) const
{
	int value = 0, bit;

	for (bit = bits - 1; bit >= 0; bit--)
		value |= rb_read_bit(rb) << bit;

	return value;
}

int AV1Codec::read_obu_size(const uint8_t *data, size_t bytes_available, size_t *const obu_size, size_t *const length_field_size) const
{
	uint64_t u_obu_size = 0;

	if (uleb_decode(data, bytes_available, &u_obu_size, length_field_size) != 0) {
		return -1;
	}

	if (u_obu_size > ULONG_MAX)
		return -1;

	*obu_size = (size_t) u_obu_size;

	return 0;
}

// Parses OBU header and stores values in 'header'.
int AV1Codec::read_obu_header(struct read_bit_buffer *rb, int is_annexb, struct ObuHeader *header) const
{
	int bit_buffer_byte_length;

	if (!rb || !header)
		return -1;

	bit_buffer_byte_length = rb->bit_buffer_end - rb->bit_buffer;

	if (bit_buffer_byte_length < 1)
		return -1;

	header->size = 1;

	if (rb_read_bit(rb) != 0) {
		// Forbidden bit. Must not be set.
		return -1;
	}

	header->type = (enum OBU_TYPE) rb_read_literal(rb, 4);
	if (!valid_obu_type(header->type))
		return -1;

	header->has_extension = rb_read_bit(rb);
	header->has_size_field = rb_read_bit(rb);

	if (!header->has_size_field && !is_annexb) {
		// section 5 obu streams must have obu_size field set.
		return -1;
	}

	if (rb_read_bit(rb) != 0) {
		// obu_reserved_1bit must be set to 0.
		return -1;
	}

	if (header->has_extension) {
		if (bit_buffer_byte_length == 1)
			return -1;

		header->size += 1;
		header->temporal_layer_id = rb_read_literal(rb, 3);
		header->spatial_layer_id = rb_read_literal(rb, 2);
		if (rb_read_literal(rb, 3) != 0) {
			// extension_header_reserved_3bits must be set to 0.
			return -1;
		}
	}

	return 0;
}

int AV1Codec::read_obu_header_and_size(const uint8_t *data, size_t bytes_available, int is_annexb,
	struct ObuHeader *obu_header, size_t *const payload_size, size_t *const bytes_read) const
{
	size_t length_field_size_obu = 0;
	size_t length_field_size_payload = 0;
	size_t obu_size = 0;
	int status = 0;
	struct read_bit_buffer rb = { data + length_field_size_obu, data + bytes_available, 0};

	if (is_annexb) {
		// Size field comes before the OBU header, and includes the OBU header
		status = read_obu_size(data, bytes_available, &obu_size, &length_field_size_obu);
		if (status != 0)
			return status;
	}

	status = read_obu_header(&rb, is_annexb, obu_header);
	if (status != 0)
		return status;

	if (!obu_header->has_size_field) {
		// Derive the payload size from the data we've already read
		if (obu_size < obu_header->size)
			return -1;

		*payload_size = obu_size - obu_header->size;
	} else {
		// Size field comes after the OBU header, and is just the payload size
		status = read_obu_size(data + length_field_size_obu + obu_header->size,
			bytes_available - length_field_size_obu - obu_header->size,
			payload_size, &length_field_size_payload);
		if (status != 0)
			return status;
	}

	*bytes_read = length_field_size_obu + obu_header->size + length_field_size_payload;

	return 0;
}

int AV1Codec::av1_parser_frame(int is_annexb, uint8_t *data, const uint8_t *data_end,
		uint8_t *dst_data, uint32_t *frame_len, uint8_t *meta_buf, uint32_t *meta_len) const
{
	int frame_decoding_finished = 0;
	uint32_t obu_size = 0;
	int seen_frame_header = 0;
	struct DataBuffer obu_size_hdr;
	uint8_t header[20] = {0};
	uint8_t *p = NULL;
	uint32_t rpu_size = 0;
	struct ObuHeader obu_header;

	memset(&obu_header, 0, sizeof(obu_header));

	// decode frame as a series of OBUs
	while (!frame_decoding_finished) {
		//	struct read_bit_buffer rb;
		size_t payload_size = 0;
		size_t header_size = 0;
		size_t bytes_read = 0;
		const size_t bytes_available = data_end - data;
		enum OBU_METADATA_TYPE meta_type;
		int status;
		uint64_t type = 0;
		uint32_t i;

		if (bytes_available == 0 && !seen_frame_header) {
			break;
		}

		status = read_obu_header_and_size(data, bytes_available, is_annexb,
			&obu_header, &payload_size, &bytes_read);
		if (status != 0) {
			return -1;
		}

		// Record obu size header information.
		obu_size_hdr.data = data + obu_header.size;
		obu_size_hdr.size = bytes_read - obu_header.size;

		// Note: read_obu_header_and_size() takes care of checking that this
		// doesn't cause 'data' to advance past 'data_end'.

		if ((size_t)(data_end - data - bytes_read) < payload_size) {
			return -1;
		}

		if (!is_annexb) {
			obu_size = bytes_read + payload_size + 4;
			header_size = 20;
		} else {
			obu_size = bytes_read + payload_size;
			header_size = 16;
		}

		header[0] = ((obu_size + 4) >> 24) & 0xff;
		header[1] = ((obu_size + 4) >> 16) & 0xff;
		header[2] = ((obu_size + 4) >> 8) & 0xff;
		header[3] = ((obu_size + 4) >> 0) & 0xff;
		header[4] = header[0] ^ 0xff;
		header[5] = header[1] ^ 0xff;
		header[6] = header[2] ^ 0xff;
		header[7] = header[3] ^ 0xff;
		header[8] = 0;
		header[9] = 0;
		header[10] = 0;
		header[11] = 1;
		header[12] = 'A';
		header[13] = 'M';
		header[14] = 'L';
		header[15] = 'V';

		// put new size to here as annexb
		header[16] = (obu_size & 0xff) | 0x80;
		header[17] = ((obu_size >> 7) & 0xff) | 0x80;
		header[18] = ((obu_size >> 14) & 0xff) | 0x80;
		header[19] = ((obu_size >> 21) & 0xff) | 0x00;

		memcpy(dst_data, header, header_size);
		dst_data += header_size;
		memcpy(dst_data, data, bytes_read + payload_size);
		dst_data += (bytes_read + payload_size);

		data += bytes_read;
		*frame_len += (header_size + bytes_read + payload_size);

		switch (obu_header.type) {
			case OBU_TEMPORAL_DELIMITER:
				seen_frame_header = 0;
				break;
			case OBU_SEQUENCE_HEADER:
				// The sequence header should not change in the middle of a frame.
				if (seen_frame_header) {
					return -1;
				}
				break;
			case OBU_FRAME_HEADER:
				if (data_end == data + payload_size) {
					frame_decoding_finished = 1;
				} else {
					seen_frame_header = 1;
				}
				break;
			case OBU_REDUNDANT_FRAME_HEADER:
			case OBU_FRAME:
				if (obu_header.type == OBU_REDUNDANT_FRAME_HEADER) {
					if (!seen_frame_header) {
						return -1;
					}
				} else {
					// OBU_FRAME_HEADER or OBU_FRAME.
					if (seen_frame_header) {
						return -1;
					}
				}
				if (obu_header.type == OBU_FRAME) {
					if (data_end == data + payload_size) {
						frame_decoding_finished = 1;
						seen_frame_header = 0;
					}
				}
				break;
			case OBU_TILE_GROUP:
				if (!seen_frame_header) {
					return -1;
				}
				if (data + payload_size == data_end)
					frame_decoding_finished = 1;
				if (frame_decoding_finished)
					seen_frame_header = 0;
				break;
			case OBU_METADATA:
				uleb_decode(data, 8, &type, &bytes_read);
				if (type < 6)
					meta_type = (OBU_METADATA_TYPE) type;
				else
					meta_type = OBU_METADATA_TYPE_RESERVED_0;

				p = data + bytes_read;

				if (meta_type == OBU_METADATA_TYPE_ITUT_T35) {
	#if 0 /* for dumping original obu payload */
					for (i = 0; i < payload_size - bytes_read; i++) {
						pr_info("%02x ", p[i]);
						if (i % 16 == 15)
							pr_info("\n");
					}
					if (i % 16 != 0)
						pr_info("\n");
	#endif
					if ((p[0] == 0xb5) /* country code */
						&& ((p[1] == 0x00) && (p[2] == 0x3b)) /* terminal_provider_code */
						&& ((p[3] == 0x00) && (p[4] == 0x00) && (p[5] == 0x08) && (p[6] == 0x00))) { /* terminal_provider_oriented_code */
						meta_buf[0] = meta_buf[1] = meta_buf[2] = 0;
						meta_buf[3] = 0x01;
						meta_buf[4] = 0x19;

						if (p[11] & 0x10) {
							rpu_size = 0x100;
							rpu_size |= (p[11] & 0x0f) << 4;
							rpu_size |= (p[12] >> 4) & 0x0f;
							if (p[12] & 0x08) {
								break;
							}
							for (i = 0; i < rpu_size; i++) {
								meta_buf[5 + i] = (p[12 + i] & 0x07) << 5;
								meta_buf[5 + i] |= (p[13 + i] >> 3) & 0x1f;
							}
							rpu_size += 5;
						} else {
							rpu_size = (p[10] & 0x1f) << 3;
							rpu_size |= (p[11] >> 5) & 0x07;
							for (i = 0; i < rpu_size; i++) {
								meta_buf[5 + i] = (p[11 + i] & 0x0f) << 4;
								meta_buf[5 + i] |= (p[12 + i] >> 4) & 0x0f;
							}
							rpu_size += 5;
						}
						*meta_len = rpu_size;
					}
				} else if (meta_type == OBU_METADATA_TYPE_HDR_CLL) {
					CLog::Log(LOGDEBUG, "AV1: hdr10 cll:");
					CLog::Log(LOGDEBUG, "AV1: max_cll = {}", (p[0] << 8) | p[1]);
					CLog::Log(LOGDEBUG, "AV1: max_fall = {}", (p[2] << 8) | p[3]);
				} else if (meta_type == OBU_METADATA_TYPE_HDR_MDCV) {
					CLog::Log(LOGDEBUG, "AV1: hdr10 primaries[r,g,b] = ");
					for (i = 0; i < 3; i++) {
						CLog::Log(LOGDEBUG, "       0x{:x}, 0x{:x}\n", (p[i * 4] << 8) | p[i * 4 + 1], (p[i * 4 + 2] << 8) | p[i * 4 + 3]);
					}

					CLog::Log(LOGDEBUG, "AV1: white point = 0x{:x}, 0x{:x}\n", (p[12] << 8) | p[13], (p[14] << 8) | p[15]);
					CLog::Log(LOGDEBUG, "AV1: maxl = 0x{:x}\n", (p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19]);
					CLog::Log(LOGDEBUG, "AV1: minl = 0x{:x}\n", (p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23]);
				}
				break;
			case OBU_TILE_LIST:
				break;
			case OBU_PADDING:
				break;
			default:
				// Skip unrecognized OBUs
				break;
		}

		data += payload_size;
	}

	return 0;
}
