# EV33 website image: C++17 engine + C++17 web server. Render builds and runs this.
# The build stage also runs the unit, integration, adversarial and front-end test suites: a failing test
# stops the deploy.
FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make cmake ninja-build \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEV33_GUI=OFF \
      -DEV33_BUILD_BENCHMARKS=OFF -DEV33_BUILD_GBENCH=OFF -DEV33_BUILD_TESTS=ON -DEV33_BUILD_WEB=ON \
 && cmake --build build --target ev33_web ev33_unit_tests ev33_integration_tests ev33_adversarial_tests ev33_frontend_tests \
 && ctest --test-dir build --output-on-failure

FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends libstdc++6 tzdata \
 && rm -rf /var/lib/apt/lists/*
ENV TZ=Asia/Kolkata
WORKDIR /app
COPY --from=build /src/build/ev33_web /app/ev33_web
COPY data/scenarios /app/data/scenarios
COPY data/sample /app/data/sample
COPY benchmarks/results.csv /app/benchmarks/results.csv
COPY assets /app/assets
EXPOSE 10000
CMD ["/app/ev33_web"]
