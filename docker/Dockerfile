ARG BASE_IMAGE="ubuntu:24.04"

# Build stage
FROM ${BASE_IMAGE} AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends build-essential cmake libssl-dev && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j"$(nproc)"

# Runtime stage
FROM ${BASE_IMAGE} AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends libstdc++6 libssl3 ca-certificates && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

RUN groupadd -r oc && useradd -r -g oc oc && \
    mkdir -p /opt/qubic/oc-machine/data && \
    chown -R oc:oc /opt/qubic/oc-machine

COPY --from=build /src/build/bin/oc_machine_node /opt/qubic/oc-machine/bin/

ENV PATH="/opt/qubic/oc-machine/bin:${PATH}"

USER oc
# Node writes its sink file (mock_oc_sink.txt) to the working directory.
WORKDIR /opt/qubic/oc-machine/data

EXPOSE 21841

CMD ["oc_machine_node"]
