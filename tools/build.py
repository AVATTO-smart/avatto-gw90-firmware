#!/usr/bin/env python3
from subprocess import call
Import("env")
import shutil
import os
import time
from glob import glob

def after_build(source, target, env):
    time.sleep(2)
    shutil.copy(firmware_source, 'bin/firmware.bin')
    for f in glob ('bin/AVATTO-GW90-Ti*.bin'):
        os.unlink (f)

    # Get partitions.bin from build directory
    build_dir = env.subst("$BUILD_DIR")
    partitions_source = os.path.join(build_dir, "partitions.bin")
    partitions_dest = 'bin/partitions.bin'
    if os.path.exists(partitions_source):
        shutil.copy(partitions_source, partitions_dest)
    
    # Get bootloader.bin from build directory
    bootloader_source = os.path.join(build_dir, "bootloader.bin")
    bootloader_dest = 'bin/bootloader_dio_40m.bin'
    if os.path.exists(bootloader_source):
        shutil.copy(bootloader_source, bootloader_dest)
    
    exit_code = call("python tools/merge_bin_esp.py --output_folder ./bin --output_name AVATTO-GW90-Ti.bin --bin_path bin/bootloader_dio_40m.bin bin/partitions.bin bin/firmware.bin --bin_address 0x1000 0x8000 0x10000", shell=True)
    
    VERSION_FILE = 'tools/version'
    try:
        with open(VERSION_FILE) as FILE:
            VERSION_NUMBER = FILE.readline()
    except:
        print('No version file found')
        VERSION_NUMBER = '0.0.0'

    NEW_NAME_FULL = 'bin/AVATTO-GW90-Ti_v'+VERSION_NUMBER+'.full.bin'
    NEW_NAME = 'bin/AVATTO-GW90-Ti.bin'

    shutil.move('bin/AVATTO-GW90-Ti.bin', NEW_NAME_FULL)
    shutil.move('bin/firmware.bin', NEW_NAME)

    print('')
    print('--------------------------------------------------------')
    print('{} created with success !'.format(str(NEW_NAME_FULL)))
    print('{} created with success !'.format(str(NEW_NAME)))
    print('--------------------------------------------------------')
    print('')

env.AddPostAction("buildprog", after_build)

firmware_source = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")