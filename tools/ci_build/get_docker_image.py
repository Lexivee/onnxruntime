#!/usr/bin/env python3
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

import argparse
import collections
import contextlib
import hashlib
import os
import shlex
import subprocess
import sys
from logger import get_logger


SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
REPO_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))

sys.path.append(os.path.join(REPO_DIR, "tools", "python"))


from util import az  # noqa: E402


log = get_logger("get_docker_image")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Gets a docker image, either by building it locally or "
        "pulling it from a container registry.")

    parser.add_argument(
        "--dockerfile", default="Dockerfile", help="Path to the Dockerfile.")
    parser.add_argument(
        "--context", default=".", help="Path to the build context.")
    parser.add_argument(
        "--docker-build-args", default="",
        help="String of Docker build args which may affect the image content. "
        "These will be used in differentiating images from one another."
        "For example, '--build-arg'.")
    parser.add_argument(
        "--docker-build-args-not-affecting-image-content", default="",
        help="String of Docker build args which do not affect the image "
        "content.")

    parser.add_argument(
        "--container-registry", required=True,
        help="The Azure container registry name.")
    parser.add_argument(
        "--repository", required=True,
        help="The repository name within the Azure container registry.")

    parser.add_argument(
        "--docker-path", default="docker", help="Path to docker.")
    parser.add_argument(
        "--az-path", default="az", help="Path to az client.")

    return parser.parse_args()


FileInfo = collections.namedtuple('FileInfo', ['path', 'mode'])


def file_info_metadata_str(file_info: FileInfo):
    return "{} {}".format(file_info.path, file_info.mode)


def make_file_info_from_path(file_path: str):
    return FileInfo(file_path, os.stat(file_path).st_mode)


def update_hash_with_directory(dir_file_info: FileInfo, hash_obj):
    hash_obj.update(file_info_metadata_str(dir_file_info).encode())

    files, dirs = [], []
    with os.scandir(dir_file_info.path) as dir_it:
        for dir_entry in dir_it:
            file_info = FileInfo(dir_entry.path, dir_entry.stat().st_mode)
            if dir_entry.is_dir():
                dirs.append(file_info)
            elif dir_entry.is_file():
                files.append(file_info)

    def file_info_key(file_info: FileInfo):
        return file_info.path

    files.sort(key=file_info_key)
    dirs.sort(key=file_info_key)

    for file_info in files:
        update_hash_with_file(file_info, hash_obj)

    for file_info in dirs:
        update_hash_with_directory(file_info, hash_obj)


def update_hash_with_file(file_info: FileInfo, hash_obj):
    hash_obj.update(file_info_metadata_str(file_info).encode())

    read_bytes_length = 8192
    with open(file_info.path, mode="rb") as file_data:
        while True:
            read_bytes = file_data.read(read_bytes_length)
            if len(read_bytes) == 0:
                break
            hash_obj.update(read_bytes)


def generate_tag(dockerfile_path, context_path, docker_build_args_str):
    hash_obj = hashlib.sha256()
    hash_obj.update(docker_build_args_str.encode())
    update_hash_with_file(
        make_file_info_from_path(dockerfile_path), hash_obj)
    update_hash_with_directory(
        make_file_info_from_path(context_path), hash_obj)
    return "image_content_digest_{}".format(hash_obj.hexdigest())


def container_registry_has_image(container_registry, repository, tag, az_path):
    existing_repositories = az(
        "acr", "repository", "list", "--name", container_registry,
        az_path=az_path)

    if not repository in existing_repositories:
        return False

    existing_tags = az(
        "acr", "repository", "show-tags",
        "--name", container_registry, "--repository", repository,
        az_path=az_path)

    if not tag in existing_tags:
        return False

    return True


def run(*args):
    cmd = [*args]
    log.debug("Running command: {}".format(cmd))
    subprocess.run(cmd, check=True)


@contextlib.contextmanager
def docker_login_logout(container_registry, docker_path):
    az("acr", "login", "--name", container_registry, parse_output=False)
    try:
        yield
    finally:
        run(docker_path, "logout")


def main():
    args = parse_args()

    tag = generate_tag(args.dockerfile, args.context, args.docker_build_args)

    full_image_name = "{}.azurecr.io/{}:{}".format(
        args.container_registry, args.repository, tag)

    log.info("Image: {}".format(full_image_name))

    if container_registry_has_image(
            args.container_registry, args.repository, tag, args.az_path):
        log.info("Image found, pulling...")

        with docker_login_logout(args.container_registry, args.docker_path):
            run(args.docker_path, "pull", full_image_name)
    else:
        log.info("Image not found, building and pushing...")

        run(args.docker_path, "build",
            *shlex.split(args.docker_build_args),
            *shlex.split(args.docker_build_args_not_affecting_image_content),
            "--tag", full_image_name,
            "--file", args.dockerfile,
            args.context)

        with docker_login_logout(args.container_registry, args.docker_path):
            run(args.docker_path, "push", full_image_name)

    return 0


if __name__ == "__main__":
    sys.exit(main())
