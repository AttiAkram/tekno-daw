# SPDX-License-Identifier: AGPL-3.0-or-later
#
# A clean Linux machine for BUILDING and TESTING tekno-daw (ADR 0013). Not for running it:
# a DAW needs low-latency audio and a display, which containers do badly.
#
#   Full clean-room check (configure + build + all tests, window tests under Xvfb):
#     docker build -f docker/Dockerfile --target check .
#
#   Interactive build environment with the repo mounted:
#     docker build -f docker/Dockerfile --target env -t tekno-build .
#     docker run --rm -it -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src tekno-build \
#         xvfb-run -a cmake --workflow --preset ci

FROM ubuntu:24.04 AS env
COPY scripts/install-deps-ubuntu.sh /tmp/install-deps-ubuntu.sh
RUN /tmp/install-deps-ubuntu.sh && rm -rf /var/lib/apt/lists/* /tmp/install-deps-ubuntu.sh

FROM env AS check
WORKDIR /src
# Only what building needs: nothing else in the working directory can end up in the image.
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake ./cmake
COPY assets ./assets
COPY src ./src
COPY tests ./tests
RUN xvfb-run -a cmake --workflow --preset ci
