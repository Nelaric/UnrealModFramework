# Copyright (c) 2026 Nelaric Contributors
FROM ghcr.io/epicgames/unreal-engine:dev-slim-5.6.1
USER root
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl cmake g++ make python3 \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /usr/share/keyrings/llvm.asc \
    && . /etc/os-release \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.asc] https://apt.llvm.org/$VERSION_CODENAME/ llvm-toolchain-$VERSION_CODENAME-18 main" > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends clang-18 libclang-18-dev \
    && rm -rf /var/lib/apt/lists/*
USER ue4
