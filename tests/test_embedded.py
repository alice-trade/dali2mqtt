import pytest
from pytest_embedded import Dut

@pytest.mark.esp32s3
@pytest.mark.esp32c6
@pytest.mark.esp32c3
def test_unity_embedded_firmware(dut: Dut) -> None:
    dut.expect_exact('Starting Unity Tests...', timeout=10)
    dut.expect_unity_test_output(timeout=60)