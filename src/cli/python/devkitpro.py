from toolchain import Toolchain
from linker import DevkitProLinker


class DevkitProToolchain(Toolchain):

    def __init__(self, target):
        super().__init__(target)

        self.keys = {}

    def get_linker(self):
        return DevkitProLinker()
