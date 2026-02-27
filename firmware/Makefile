# CellSim Card Firmware — Container-based Build System
#
# Uses Podman (or Docker) with the official Zephyr build image.
# All build tools live inside the container; the host only needs Podman.
#
# Architecture:
#   - Zephyr SDK + source tree live in a persistent Podman volume ("zephyr-workspace")
#   - Your app source is bind-mounted read-write from the host
#   - west.yml is copied into the volume so west init works cleanly
#   - Build output goes to firmware/build/ (inside the firmware bind-mount)
#
# Usage:
#   make setup     — Pull image + initialize west workspace (first time, ~5 min)
#   make build     — Build firmware for nucleo_h723zg
#   make clean     — Remove build artifacts
#   make shell     — Interactive shell inside the build container
#   make menuconfig — Run Zephyr menuconfig (TUI Kconfig editor)
#   make flash     — Flash via ST-Link (requires ST-Link on host)

# --- Configuration ---
ZEPHYR_IMAGE   ?= docker.io/zephyrprojectrtos/zephyr-build:v0.27.4
VOLUME_NAME    ?= zephyr-workspace
BOARD          ?= nucleo_h723zg
CONTAINER_RT   ?= podman

# Paths (relative to this Makefile)
MAKEFILE_DIR   := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
FIRMWARE_DIR   := $(abspath $(MAKEFILE_DIR))
BUILD_DIR      := $(FIRMWARE_DIR)/build

# Common container run flags
# - entrypoint bypassed (it starts Xvfb/VNC, not needed for CLI builds)
# - runs as container root (rootless Podman maps this to host user)
# - SELinux label disabled to avoid permission issues with bind mounts
RUN_FLAGS      := --rm \
    --entrypoint="" \
    --user 0:0 \
    --security-opt label=disable \
    -v $(VOLUME_NAME):/workdir/ws \
    -v $(FIRMWARE_DIR):/workdir/firmware \
    -w /workdir/ws \
    -e ZEPHYR_BASE=/workdir/ws/zephyr \
    -e ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-0.17.0 \
    -e HOME=/tmp/zhome \
    $(ZEPHYR_IMAGE)

# Build dir path inside container
CBUILD := /workdir/firmware/build

# --- Targets ---

.PHONY: setup build clean shell menuconfig flash help

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-15s\033[0m %s\n", $$1, $$2}'

setup: ## Pull Zephyr image + initialize west workspace (first time)
	@echo "==> Pulling Zephyr build image..."
	$(CONTAINER_RT) pull $(ZEPHYR_IMAGE)
	@echo "==> Creating persistent volume '$(VOLUME_NAME)'..."
	$(CONTAINER_RT) volume inspect $(VOLUME_NAME) >/dev/null 2>&1 || \
		$(CONTAINER_RT) volume create $(VOLUME_NAME)
	@echo "==> Initializing west workspace..."
	$(CONTAINER_RT) run $(RUN_FLAGS) \
		bash -c '\
			mkdir -p /tmp/zhome && \
			mkdir -p /workdir/ws/card && \
			cp /workdir/firmware/card/west.yml /workdir/ws/card/west.yml && \
			if [ ! -d /workdir/ws/.west ]; then \
				echo "First-time init: west init + update (this takes a few minutes)..."; \
				west init -l /workdir/ws/card && \
				west update --narrow -o=--depth=1 && \
				west zephyr-export; \
			else \
				echo "West workspace exists, running update..."; \
				west update --narrow -o=--depth=1; \
			fi'
	@echo "==> Setup complete."

build: ## Build firmware for $(BOARD)
	@mkdir -p $(BUILD_DIR)
	$(CONTAINER_RT) run $(RUN_FLAGS) \
		bash -c '\
			mkdir -p /tmp/zhome /workdir/ws/card && \
			cp /workdir/firmware/card/west.yml /workdir/ws/card/west.yml && \
			west build -b $(BOARD) /workdir/firmware/card \
				-d $(CBUILD) \
			|| exit 1; \
			echo ""; \
			echo "==> Build artifacts:"; \
			ls -lh $(CBUILD)/zephyr/zephyr.{bin,elf,hex} 2>/dev/null'

clean: ## Remove build artifacts
	rm -rf $(BUILD_DIR)
	@echo "==> Build directory cleaned."

shell: ## Interactive shell inside the Zephyr build container
	$(CONTAINER_RT) run -it $(RUN_FLAGS) \
		bash -c '\
			mkdir -p /tmp/zhome /workdir/ws/card && \
			cp /workdir/firmware/card/west.yml /workdir/ws/card/west.yml && \
			echo "Zephyr workspace: /workdir/ws" && \
			echo "App source:       /workdir/firmware/card" && \
			echo "Build dir:        $(CBUILD)" && \
			exec bash'

menuconfig: ## Run Zephyr menuconfig (TUI Kconfig editor)
	@mkdir -p $(BUILD_DIR)
	$(CONTAINER_RT) run -it $(RUN_FLAGS) \
		bash -c '\
			mkdir -p /tmp/zhome /workdir/ws/card && \
			cp /workdir/firmware/card/west.yml /workdir/ws/card/west.yml && \
			west build -b $(BOARD) /workdir/firmware/card \
				-d $(CBUILD) -t menuconfig'

flash: ## Flash firmware via ST-Link (requires ST-Link connected to host)
	@if [ ! -f $(BUILD_DIR)/zephyr/zephyr.bin ]; then \
		echo "Error: No firmware binary found. Run 'make build' first."; \
		exit 1; \
	fi
	@echo "==> Flashing $(BUILD_DIR)/zephyr/zephyr.bin via ST-Link..."
	$(CONTAINER_RT) run --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		$(RUN_FLAGS) \
		bash -c '\
			mkdir -p /tmp/zhome && \
			west flash -d $(CBUILD)'
