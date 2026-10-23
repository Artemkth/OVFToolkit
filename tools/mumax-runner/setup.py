"""Setuptools hooks for the platform-specific mumax slave wheel."""

from setuptools import setup
from wheel.bdist_wheel import bdist_wheel


class PlatformWheel(bdist_wheel):
    """Tag bundled native executables by platform, but not Python ABI."""

    def finalize_options(self) -> None:
        super().finalize_options()
        self.root_is_pure = False

    def get_tag(self) -> tuple[str, str, str]:
        _, _, platform_tag = super().get_tag()
        return "py3", "none", platform_tag


setup(cmdclass={"bdist_wheel": PlatformWheel})
