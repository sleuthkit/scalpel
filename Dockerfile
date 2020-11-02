FROM ubuntu:16.04

# ADD https://github.com/sleuthkit/scalpel/archive/master.zip /

RUN apt-get update && \
  apt-get install -y -qq --no-install-recommends \
      automake \
      default-jdk \
      g++ \
      libtool \
      libtre-dev \
      make \
      unzip && \
  rm -rf /var/lib/apt/lists/*

COPY . /scalpel
WORKDIR /scalpel
RUN ./bootstrap && ./configure --disable-shared && make
ENTRYPOINT ["/scalpel/entrypoint.sh"]
