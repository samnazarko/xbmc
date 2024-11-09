/*
 *  Copyright (C) 2024 OSMC
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "AMLInsecureVideoCodec.h"


class amlogic::DVCodec : public amlogic::AMLInsecureVideoCodec
{
private:
	bool m_player_supports_dv;
	bool m_dv_enabled;

protected:
	virtual inline bool isDolbyVisionSupported() const {
		return m_player_supports_dv;
	}

	virtual bool isDisplaySupportsDolbyVision() const;

	void setupVideoCodecParams(aml_generic_param &params) const;

private:
	void setupDolbyVision(const CDVDStreamInfo &hints);

public:
	DVCodec(CProcessInfo &processInfo, const CDVDStreamInfo &hints);
	virtual ~DVCodec();
};
