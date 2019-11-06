import pytest
import os
import time

# =========================== variables =======================================

# =========================== test ============================================

def syscall(cmd):
    print '>>> {0}'.format(cmd)
    return os.system(cmd)
    
# ==== tests

def test_compilation():
    syscall("echo compilation...")
    result = syscall("%KEIL_UV_DIR%\\UV4.exe -b scm_v3c\\applications\\freq_sweep_tx\\freq_sweep_tx.uvprojx -o \"output.txt\"")
    assert result == 0
    
def test_bootload():
    syscall("echo bootload SCuM...")
    
    result = syscall("python scm_v3c\\bootload\\bootload.py -tp %PORT_TEENSY% -i scm_v3c\\applications\\freq_sweep_tx\\freq_sweep_tx.bin")
    assert result == 0
    
    syscall("echo bootload OpenMote...")
    
    result = syscall("python scm_v3c\\bootload\\cc2538-bsl.py  -e --bootloader-invert-lines -w -b 400000 -p %PORT_OPENMOTE% scm_v3c\\applications\\freq_sweep_tx\\openmote-b-24ghz.ihex")
    assert result == 0

def test_logData():
    syscall("echo log output from OpenMote serial port...")

    result = syscall("python scm_v3c\\applications\\freq_sweep_tx\\freq_sweep_tx_logger.py")
    assert result == 0
        
def test_verifyResult():

    syscall("echo verify  OpenMote serial output...")

    result = syscall("python scm_v3c\\applications\\freq_sweep_tx\\freq_sweep_tx_parser.py")
    assert result == 0
    
    syscall("echo Frequency Sweep Test complete!")
