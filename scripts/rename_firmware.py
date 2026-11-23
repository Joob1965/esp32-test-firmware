Import("env")

import shutil
from pathlib import Path


firmware_name = env.GetProjectOption("custom_firmware_name", "")
if firmware_name:
    binary_path = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
    destination = Path(env.subst("$PROJECT_DIR/firmware")) / firmware_name

    def copy_named_firmware(source, target, env):
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(binary_path, destination)
        print(f"Created device firmware: {destination}")

    env.AddPostAction(str(binary_path), copy_named_firmware)
