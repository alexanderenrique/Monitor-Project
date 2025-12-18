Import("env")
import os
import shutil

# Find LVGL library directory
libdeps_dir = os.path.join(env["PROJECT_BUILD_DIR"], "..", "libdeps", env["PIOENV"])
libdeps_path = os.path.abspath(libdeps_dir)
lvgl_dir = os.path.join(libdeps_path, "lvgl")
helium_dir = os.path.join(lvgl_dir, "src", "draw", "sw", "blend", "helium")

# Remove Helium directory if it exists (ARM-specific, not for ESP32/Xtensa)
if os.path.exists(helium_dir):
    print(f"Removing Helium directory (ARM-specific, incompatible with ESP32): {helium_dir}")
    shutil.rmtree(helium_dir)

