/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <string>

#include "namespace.h"

class amlogic::PosixFile
{
private:
	int m_fd;

public:
	PosixFile() : m_fd(-1) {}

	PosixFile(int fd) : m_fd(fd) {}

	~PosixFile() {
		if (m_fd >= 0) {
			close(m_fd);
		}
	}

	bool Open(const std::string &pathName, int flags) {
		m_fd = open(pathName.c_str(), flags);
		return m_fd >= 0;
	}

	int GetDescriptor() const {
		return m_fd;
	}

	int IOControl(unsigned long request, void *param) {
		return ioctl(m_fd, request, param);
	}
};

