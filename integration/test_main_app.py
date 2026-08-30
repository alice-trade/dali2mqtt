import pytest
from pytest_embedded import Dut

@pytest.mark.esp32s3
@pytest.mark.esp32c6
@pytest.mark.esp32c3
def test_main_firmware_boot_and_provisioning(dut: Dut) -> None:
    dut.expect(r'DALI-to-MQTT Bridge v\.\d+\.\d+\.\d+', timeout=10)
    dut.expect_exact('NVS and FS initialized successfully.', timeout=5)

    res = dut.expect([
        r'Device is configured\. Starting normal mode\.',
        r'Device is not configured\. Starting provisioning mode\.'
    ], timeout=10)

    if res.group(0).startswith(b'Device is not configured'):
        dut.expect_exact('AP Mode started.', timeout=5)
        dut.expect_exact('Web service for provisioning is running.', timeout=5)
    else:
        dut.expect_exact('Dali Adaptive Sync Task Started.', timeout=10)
        dut.expect_exact('Application setup complete. Logic running in background tasks.', timeout=5)