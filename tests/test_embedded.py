#  Copyright (c) 2026 Alice-Trade Inc.
#  SPDX-License-Identifier: GPL-3.0-or-later

import re
import pytest
import time
from pytest_embedded import Dut

UNITY_TEST_REGEX = re.compile(
    r'(?P<file>[a-zA-Z0-9_./\\-]+):(?P<line>\d+):(?P<test>\w+):(?P<status>PASS|FAIL|IGNORE)(?::\s*(?P<msg>.*))?'
)

def reset_esp(dut: Dut) -> None:
    proc = getattr(dut.serial, 'proc', dut.serial)
    if hasattr(proc, 'setRTS') and hasattr(proc, 'setDTR'):
        proc.setDTR(False)
        proc.setRTS(True)
        time.sleep(0.15)
        proc.setRTS(False)
        time.sleep(0.1)


@pytest.mark.esp32s3
@pytest.mark.esp32c6
@pytest.mark.esp32c3
def test_unity_embedded_firmware(dut: Dut) -> None:
    reset_esp(dut)
    dut.expect_exact('Initializing hardware environment for Unity tests...', timeout=10)
    dut.expect_exact('Starting Unity Tests in dedicated task...', timeout=5)

    res = dut.expect([
        r'Guru Meditation Error',
        r'abort\(\) was called',
        r'-----------------------\s+(?P<total>\d+) Tests (?P<fail>\d+) Failures (?P<ignore>\d+) Ignored',
    ], timeout=25)

    match_str = res.group(0).decode('utf-8', errors='ignore')

    if 'Guru Meditation Error' in match_str or 'abort()' in match_str:
        panic_dump = dut.read_all().decode('utf-8', errors='ignore')
        pytest.fail(f"\n[FAIL-FAST] ESP32 Core Panic detected!\n{match_str}\n{panic_dump}")

    raw_before = dut.pexpect_proc.before
    full_log = raw_before.decode('utf-8', errors='ignore') if isinstance(raw_before, bytes) else str(raw_before)
    passed_tests = []
    failed_tests = []
    ignored_tests = []

    for match in UNITY_TEST_REGEX.finditer(full_log):
        test_name = match.group('test')
        status = match.group('status')
        file_name = match.group('file')
        line_no = match.group('line')
        message = match.group('msg') or ""

        if status == 'PASS':
            passed_tests.append(test_name)
        elif status == 'FAIL':
            failed_tests.append({
                'name': test_name,
                'file': file_name,
                'line': line_no,
                'msg': message
            })
        elif status == 'IGNORE':
            ignored_tests.append(test_name)

    dut.expect_exact('All unit tests executed successfully.', timeout=5)

    print(f"\n" + "=" * 60)
    print(f" Total parsed: {len(passed_tests) + len(failed_tests) + len(ignored_tests)}")
    print(f"  -> PASSED:  {len(passed_tests)}")
    print(f"  -> FAILED:  {len(failed_tests)}")
    print(f"  -> IGNORED: {len(ignored_tests)}")
    print("=" * 60)

    if failed_tests:
        failure_details = "\n".join([
            f"[-] {item['name']} FAILED at {item['file']}:{item['line']} -> {item['msg']}"
            for item in failed_tests
        ])
        pytest.fail(f"\nDetected {len(failed_tests)} Unity test failure(s):\n{failure_details}")

    assert len(failed_tests) == 0, "All Unity tests must pass"