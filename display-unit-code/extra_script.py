Import("env")
import os
import shutil

# Find library directories
libdeps_dir = os.path.join(env["PROJECT_BUILD_DIR"], "..", "libdeps", env["PIOENV"])
libdeps_path = os.path.abspath(libdeps_dir)

# Find LVGL library directory
lvgl_dir = os.path.join(libdeps_path, "lvgl")
helium_dir = os.path.join(lvgl_dir, "src", "draw", "sw", "blend", "helium")

# Remove Helium directory if it exists (ARM-specific, not for ESP32/Xtensa)
if os.path.exists(helium_dir):
    print(f"Removing Helium directory (ARM-specific, incompatible with ESP32): {helium_dir}")
    shutil.rmtree(helium_dir)

# Copy User_Setup.h to TFT_eSPI library directory so it can be found
tft_espi_dir = os.path.join(libdeps_path, "TFT_eSPI")
user_setup_src = os.path.join(env["PROJECT_DIR"], "include", "User_Setup.h")
user_setup_dst = os.path.join(tft_espi_dir, "User_Setup.h")

if os.path.exists(user_setup_src):
    if os.path.exists(tft_espi_dir):
        print(f"Copying User_Setup.h to TFT_eSPI directory: {user_setup_dst}")
        shutil.copy2(user_setup_src, user_setup_dst)
    else:
        print(f"WARNING: TFT_eSPI directory not found at {tft_espi_dir}")
else:
    print(f"WARNING: User_Setup.h not found at {user_setup_src}")

# lv_conf.h is now read directly from include/ directory via build flags
# No need to copy it anymore!

