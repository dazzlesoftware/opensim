FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake fontconfig fonts-liberation libgdiplus libfreetype-dev \
    libicu-dev libjpeg-dev libmariadb-dev libopenjp2-7 libpng-dev libpq-dev \
    libssl-dev ninja-build pkg-config python3 zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
CMD ["bash", "/src/native-port/tools/linux-preview/check.sh"]
