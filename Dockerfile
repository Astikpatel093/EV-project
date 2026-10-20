# EV33 web server image (C11 engine + C++17 server). Render builds and runs this.
FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends gcc g++ make cmake \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEV33_GUI=OFF \
 && cmake --build build --target ev33_web ev33_tests -j \
 && ./build/ev33_tests

FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends libstdc++6 tzdata \
 && rm -rf /var/lib/apt/lists/*
ENV TZ=Asia/Kolkata
WORKDIR /app
COPY --from=build /src/build/ev33_web /app/ev33_web
COPY data /app/data
COPY results/bench.csv /app/results/bench.csv
COPY assets /app/assets
EXPOSE 10000
CMD ["/app/ev33_web"]
