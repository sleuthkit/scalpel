FROM ubuntu:16.04

# ADD https://github.com/sleuthkit/scalpel/archive/master.zip /
COPY . /scalpel

RUN apt-get update && \
  apt-get install -y -qq \
      automake \
      default-jdk \
      g++ \
      libtool \
      libtre-dev \
      make \
      unzip

WORKDIR /scalpel
RUN ./bootstrap && ./configure --disable-shared && make
CMD ["./scalpel", "-o /recovery", "device.img"]
