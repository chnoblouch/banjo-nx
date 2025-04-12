from pathlib import Path

from tools import Program
from configuration import Configuration
from toolchain import Toolchain
from linker import DevkitProLinker


class ELF2NRO(Program):

    def __init__(self):
        super().__init__("elf2nro")

    def run(self, config: Configuration, toolchain: Toolchain):
        out_dir = Path("out/aarch64-nintendoswitch-devkitpro-debug")
        elf_path = str(out_dir / f"{config.name}.elf")
        nro_path = str(out_dir / f"{config.name}.nro")

        self.run_command([
            "/opt/devkitpro/tools/bin/elf2nro",
            elf_path,
            nro_path,
            "--icon=/opt/devkitpro/libnx/default_icon.jpg",
        ])


class DevkitProToolchain(Toolchain):

    def __init__(self, target):
        super().__init__(target)

        self.keys = {}

    def get_linker(self):
        return DevkitProLinker()

    def get_post_link_tool(self):
        return ELF2NRO()
