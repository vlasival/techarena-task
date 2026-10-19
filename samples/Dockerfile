FROM ubuntu:26.04

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		build-essential \
		ca-certificates \
		cargo \
		cmake \
		curl \
		g++ \
		git \
		golang-go \
		gradle \
		make \
		maven \
		ninja-build \
		openjdk-26-jdk-headless \
		pkg-config \
		python3 \
		python3-dev \
		python3-pip \
		python3-setuptools \
		python3-venv \
		python3-wheel \
		rustc \
		unzip \
		wget \
		zip \
	&& rm -rf /var/lib/apt/lists/*

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		libboost-all-dev \
		libeigen3-dev \
		libmlpack-dev \
		libz3-dev \
		nlohmann-json3-dev \
		z3 \
	&& rm -rf /var/lib/apt/lists/*

RUN wget https://scipopt.org/download/release/scipoptsuite_10.1.0-1+resolute_amd64.deb \
	&& apt-get update \
	&& apt-get install -y --no-install-recommends ./scipoptsuite_10.1.0-1+resolute_amd64.deb \
	&& rm scipoptsuite_10.1.0-1+resolute_amd64.deb \
	&& rm -rf /var/lib/apt/lists/* \
	&& wget https://github.com/ERGO-Code/HiGHS/releases/download/v1.15.1/highs-1.15.1-x86_64-linux-gnu-static-apache.tar.gz \
	&& tar -xf highs-1.15.1-x86_64-linux-gnu-static-apache.tar.gz -C /usr/local \
	&& rm highs-1.15.1-x86_64-linux-gnu-static-apache.tar.gz

ARG JULIA_VERSION=1.13.1
RUN wget -q "https://julialang-s3.julialang.org/bin/linux/x64/1.13/julia-${JULIA_VERSION}-linux-x86_64.tar.gz" \
	&& tar -xaf "julia-${JULIA_VERSION}-linux-x86_64.tar.gz" -C /usr/local \
	&& ln -sf "/usr/local/julia-${JULIA_VERSION}/bin/julia" /usr/local/bin/julia \
	&& rm "julia-${JULIA_VERSION}-linux-x86_64.tar.gz" \
	&& julia --version