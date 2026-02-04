#!/usr/bin/env python3
import os
import re
import sys
from os import _wrap_close
from typing import Literal, Match


class GitVersion:
    def __init__(
        self,
        header_path: str = None,
        macro_name: str = "VERSION",
        version_format: str = "v{full} {commit_hash}",
    ) -> None:
        self._default_version = "0.1.0a"
        os.chdir(os.path.dirname(os.path.realpath(__file__)))

        self.header_path: str = header_path or "src/kernel/kklibc/kklibc.h"
        self.macro_name: str = macro_name
        self.version_format: str = version_format

        self._configure_patterns()

    def _configure_patterns(self) -> None:
        self.macro_pattern = rf'#define {self.macro_name} "([^"]*)"'

        self.macro_template_map = {
            "full": self.full,
            "version": self.version,
            "branch": self.branch,
            "build": self.build,
            "commit_hash": self.commit_hash,
            "tag": self.tag,
            "standard": self.standard,
        }

    def _format_version_string(self) -> str:
        result: str = self.version_format
        for key, value in self.macro_template_map.items():
            placeholder: str = f"{{{key}}}"
            if placeholder in result:
                result = result.replace(placeholder, value)
        return result

    @property
    def tag(self):
        stream: _wrap_close = os.popen("git describe --match v[0-9]* --abbrev=0 --tags")
        return stream.read().strip()

    @property
    def version(self) -> str:
        version: str = f"{self.tag[1:]}-{self.build}"
        return version if version != "." else self._default_version

    @property
    def default_branch(self) -> Literal["main"] | str:
        stream: _wrap_close = os.popen("git config --get init.defaultBranch")
        result = stream.read().strip()
        return result or "main"

    @property
    def build(self):
        stream: _wrap_close = os.popen(f"git rev-list {self.tag}.. --count")
        return stream.read().strip()

    @property
    def branch(self):
        stream: _wrap_close = os.popen("git branch --show-current")
        return stream.read().strip()

    @property
    def full(self) -> str:
        return f"{self.version}-{self.branch}"

    @property
    def standard(self) -> str:
        standard: str = f"{self.version}-{self.branch}"
        if self.branch == self.default_branch or re.match("release/.*", self.branch):
            standard = f"{self.version}"
        return standard

    @property
    def commit(self) -> str:
        stream: _wrap_close = os.popen("git rev-parse HEAD")
        return stream.read().strip()

    @property
    def commit_hash(self) -> str:
        stream: _wrap_close = os.popen("git rev-parse --short HEAD")
        return stream.read().strip()

    @property
    def version_string(self) -> str:
        return self._format_version_string()

    def read_current_header_version(self) -> str | None:
        if not os.path.exists(self.header_path):
            return None

        try:
            with open(self.header_path, "r") as f:
                content: str = f.read()

            match: Match[str] | None = re.search(self.macro_pattern, content)
            return match.group(1) if match else None

        except Exception as e:
            print(f"Error reading header file: {e}")
            return None

    def write_new_version(self, new_version: str) -> bool:
        try:
            with open(self.header_path, "r") as f:
                content: str = f.read()

            new_content: str = re.sub(
                self.macro_pattern,
                f'#define {self.macro_name} "{new_version}"',
                content,
            )

            with open(self.header_path, "w") as f:
                f.write(new_content)

            return True

        except Exception as e:
            print(f"Error writing header file: {e}")
            return False

    def check_and_update_version(
        self, force_update: bool = False
    ) -> tuple[bool, str, str]:
        if not os.path.exists(self.header_path):
            return False, "", f"Header file not found at {self.header_path}"

        current_version: str = self.version_string
        existing_version: str | None = self.read_current_header_version()

        if existing_version is None:
            return (
                False,
                current_version,
                f"Macro {self.macro_name} not found in header file",
            )

        if existing_version == current_version and not force_update:
            return (
                True,
                current_version,
                f"Version already up to date: {current_version}",
            )

        if self.write_new_version(current_version):
            return (
                True,
                current_version,
                f"Updated from '{existing_version}' to '{current_version}'",
            )
        else:
            return False, current_version, "Failed to update version"

    def update_header_version(self, force_update: bool = False) -> bool:
        success, new_version, message = self.check_and_update_version(force_update)
        print(message)
        return success

    def __str__(self) -> str:
        return f"""
Tag: {self.tag}
Version: {self.version}
Full: {self.full}
Branch: {self.branch}
Build: {self.build}
Standard: {self.standard}
Commit: {self.commit}
Header Version: {self.version_string}

KintsugiOS {self.full} {self.commit_hash}
"""


if __name__ == "__main__":
    configs: dict[str, str] = {
        "header_path": "src/kernel/kklibc/kklibc.h",
        "macro_name": "VERSION",
        "version_format": "v{full} {commit_hash}",
    }

    git_version = GitVersion(**configs)

    print(f"Current version: {git_version.version_string}")

    if len(sys.argv) > 1 and sys.argv[1] == "__update_version":
        if git_version.update_header_version():
            print("Version update completed successfully")
        else:
            print("Version update failed")

    print(git_version)
