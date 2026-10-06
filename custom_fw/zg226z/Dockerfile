FROM --platform=linux/amd64 debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends make python3 bzip2 ca-certificates && rm -rf /var/lib/apt/lists/*
COPY tc32_gcc_v2.0.tar.bz2 /tmp/
RUN mkdir -p /opt/tc32 && tar -xjf /tmp/tc32_gcc_v2.0.tar.bz2 -C /opt/tc32 && rm /tmp/tc32_gcc_v2.0.tar.bz2 && ls /opt/tc32
WORKDIR /src
