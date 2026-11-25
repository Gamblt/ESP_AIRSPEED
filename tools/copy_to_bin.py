"""PlatformIO post-build hook: copy the flashable images into <project>/bin.

Registered in platformio.ini via `extra_scripts = post:tools/copy_to_bin.py`.
Copies bootloader.bin, partitions.bin and firmware.bin from the build
directory, plus boot_app0.bin from the Arduino framework package, so that
bin/flash.sh has everything it needs.
"""
import os
import shutil

Import("env")  # noqa: F821 (provided by PlatformIO/SCons)


def copy_images(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "bin")
    os.makedirs(out_dir, exist_ok=True)
    framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    images = [
        os.path.join(build_dir, "bootloader.bin"),
        os.path.join(build_dir, "partitions.bin"),
        os.path.join(framework, "tools", "partitions", "boot_app0.bin"),
        os.path.join(build_dir, "firmware.bin"),
    ]
    for image in images:
        shutil.copy2(image, out_dir)
    print("Firmware images copied to", out_dir)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_images)  # noqa: F821
