FROM debian:12

# Buildroot dependencies
RUN apt-get -qq update && apt-get -y install \
    locales lsb-release git wget make binutils gcc g++ patch gzip bzip2 perl \
    tar cpio unzip rsync file bc libssl-dev build-essential libncurses-dev \
    mtools fdisk dosfstools ccache python3 \
 && rm -rf /var/lib/apt/lists/*

RUN echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen && locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8 LANGUAGE=en_US:en LC_ALL=en_US.UTF-8


WORKDIR /work
ENTRYPOINT ["/work/build.sh"]
