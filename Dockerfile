# Linux build env (thread pinning, perf). On a Mac this runs inside Docker's Linux VM:
# pinning works inside the VM, but vCPUs are host threads, so treat numbers as indicative only.
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake make python3 util-linux linux-tools-generic ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /work
COPY . .
CMD ["scripts/run_all.sh", "linux-docker"]
