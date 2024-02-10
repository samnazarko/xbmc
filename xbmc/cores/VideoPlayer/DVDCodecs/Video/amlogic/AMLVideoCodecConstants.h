/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#define HDR_BUF_SIZE 1024

#define TRICKMODE_NONE  0x00
#define TRICKMODE_I     0x01
#define TRICKMODE_FFFB  0x02

#define P_PRE           (0x02000000)
//#define F_PRE           (0x03000000)

#define PLAYER_SUCCESS          (0)
#define PLAYER_FAILED           (-(P_PRE|0x01))
#define PLAYER_NOMEM            (-(P_PRE|0x02))
#define PLAYER_EMPTY_P          (-(P_PRE|0x03))

#define PLAYER_WR_FAILED        (-(P_PRE|0x21))
//#define PLAYER_WR_EMPTYP        (-(P_PRE|0x22))
//#define PLAYER_WR_FINISH        (P_PRE|0x1)

#define PLAYER_PTS_ERROR        (-(P_PRE|0x31))
//#define PLAYER_UNSUPPORT        (-(P_PRE|0x35))
//#define PLAYER_CHECK_CODEC_ERROR  (-(P_PRE|0x39))

#define EXTERNAL_PTS    (1)
#define SYNC_OUTSIDE    (2)
#define UNSTABLE_PTS	0x40

#define PTS_FREQ        90000
#define UNIT_FREQ       96000
#define AV_SYNC_THRESH  PTS_FREQ*30

