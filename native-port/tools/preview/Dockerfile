FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates fontconfig fonts-liberation libgdiplus libfreetype6 libicu74 libjpeg-turbo8 libmariadb3 \
    libopenjp2-7 libpng16-16t64 libpq5 libssl3t64 libstdc++6 zlib1g \
    && rm -rf /var/lib/apt/lists/*
COPY . /opt/opensim-preview/
RUN chmod +x /opt/opensim-preview/bin/opensim-native /opt/opensim-preview/start-preview.sh \
    && mkdir -p /data && ln -s /data /opt/opensim-preview/data
WORKDIR /opt/opensim-preview
ENV OPENSIM_PREVIEW_BIND=0.0.0.0 OPENSIM_PREVIEW_PORT=19093
ENTRYPOINT ["bash", "/opt/opensim-preview/start-preview.sh"]
