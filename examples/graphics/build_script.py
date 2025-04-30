from pathlib import Path
import subprocess

UAM_PATH = "/opt/devkitpro/tools/bin/uam"
IN_PATH = "shaders"
OUT_PATH = "romfs/shaders"

Path(OUT_PATH).mkdir(parents=True, exist_ok=True)
subprocess.run([UAM_PATH, f"{IN_PATH}/triangle_vert.glsl", "-s", "vert", "-o", f"{OUT_PATH}/triangle_vert.dksh"])
subprocess.run([UAM_PATH, f"{IN_PATH}/triangle_frag.glsl", "-s", "frag", "-o", f"{OUT_PATH}/triangle_frag.dksh"])
