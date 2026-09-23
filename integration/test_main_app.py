#  // Copyright (c) 2026 Alice-Trade Inc.
#  // SPDX-License-Identifier: GPL-3.0-or-later
#

import pytest
from pytest_embedded import Dut

@pytest.mark.esp32s3
@pytest.mark.esp32c6
@pytest.mark.esp32c3
def test_main_firmware_boot_and_provisioning(dut: Dut) -> None:
    dut.expect(r'DALI-to-MQTT Bridge Core v\.\d+\.\d+\.\d+', timeout=10)

    res = dut.expect([
        r'Connecting to (Wi-Fi|Ethernet) infrastructure\.\.\.',
        r'Network not configured\. Starting Provisioning AP:'
    ], timeout=15)

    if b'Starting Provisioning AP' in res.group(0):
        dut.expect_exact('Starting Web UI HTTP server...', timeout=5)
    else:
        dut.expect_exact('Starting Web UI HTTP server...', timeout=5)