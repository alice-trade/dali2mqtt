#  // Copyright (c) 2026 Alice-Trade Inc.
#  // SPDX-License-Identifier: GPL-3.0-or-later

import os
import pytest

def pytest_configure(config):
    if not config.getoption('port') and 'ESPPORT' in os.environ:
        config.option.port = os.environ['ESPPORT']