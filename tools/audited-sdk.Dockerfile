FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    git cmake ninja-build clang-18 lld-18 llvm-18 make python3 \
    python3-pyelftools wget xz-utils pkg-config sudo unzip perl \
    && rm -rf /var/lib/apt/lists/*
RUN ln -s /usr/bin/clang-18 /usr/bin/clang && \
    ln -s /usr/bin/clang++-18 /usr/bin/clang++ && \
    ln -s /usr/bin/ld.lld-18 /usr/bin/ld.lld
COPY dependency-sources /sources
COPY docs /work/docs
COPY scripts /work/scripts
RUN bash /work/scripts/install_audited_dependencies.sh /sources
ENV PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
WORKDIR /work
