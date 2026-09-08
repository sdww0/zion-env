# Makefile for Zion examples
#
# Usage:
#   make all        - build everything (hello + tests + runners)
#   make hello      - build hello example only
#   make tests      - build all test EAPPs
#   make host       - build host runners (hello-runner, test-runner)
#   make clean      - remove build artifacts
#   make run-hello        - run hello example
#   make run-test-stack   - run stack test
#   make run-test-fibonacci
#   make run-test-malloc
#   ...
#
# Pre-requisites:
#   - SDK must be built: make -C <top>/sdk
#   - Eyrie runtime must be built: make -C <top>/runtime
#   - RISC-V cross-compiler in PATH, or CROSS_COMPILE set explicitly

# ---- Paths ----
TOP          := $(shell cd .. && pwd)
SDK_DIR      ?= $(TOP)/build/sdk-install
BUILD_DIR    ?= $(TOP)/build/examples
RUNTIME_DIR  ?= $(TOP)/runtime
EXAMPLES_DIR ?= $(TOP)/examples
TESTS_DIR    ?= $(EXAMPLES_DIR)/tests

# ---- Toolchain ----
CROSS_COMPILE ?= riscv64-unknown-linux-gnu-
CC            := $(CROSS_COMPILE)gcc
CXX           := $(CROSS_COMPILE)g++

# ---- SDK ----
SDK_INCLUDE  := $(SDK_DIR)/include
LIB_EAPP     := $(SDK_DIR)/lib/libzion-eapp.a
LIB_HOST     := $(SDK_DIR)/lib/libzion-host.a
LIB_EDGE     := $(SDK_DIR)/lib/libzion-edge.a
LIB_VERIFIER := $(SDK_DIR)/lib/libzion-verifier.a

# ---- Pre-built Eyrie runtime ----
EYRIE_RT     := $(RUNTIME_DIR)/eyrie-rt
LOADER_BIN   := $(RUNTIME_DIR)/loader.bin

# ---- Common flags ----
DEBUG        ?= 0
ifeq ($(DEBUG),1)
OPT_CFLAGS   := -O0 -ggdb3
else
OPT_CFLAGS   := -O2 -g
endif
EAPP_CFLAGS  := -Wall -Werror -fno-builtin -static -mcmodel=medany -std=gnu11 \
                $(OPT_CFLAGS) -I$(SDK_INCLUDE) -I$(SDK_INCLUDE)/app
EAPP_LDFLAGS := -nostdlib -static -T $(TESTS_DIR)/app.lds
HOST_CFLAGS  := -Wall -Werror -Wno-error=cpp -static $(OPT_CFLAGS) -I$(SDK_INCLUDE)
HOST_LDFLAGS := -static

# ====================================================================
# Sanity checks

check-sdk:
	@test -f $(LIB_EAPP) || { echo "ERROR: SDK not found at $(SDK_DIR). Build SDK first."; exit 1; }
	@test -f $(LIB_HOST) || { echo "ERROR: SDK host lib not found at $(SDK_DIR). Build SDK first."; exit 1; }

check-runtime:
	@test -f $(EYRIE_RT) || { echo "ERROR: Eyrie runtime not found at $(EYRIE_RT). Build runtime first."; exit 1; }
	@test -f $(LOADER_BIN) || { echo "ERROR: loader.bin not found at $(LOADER_BIN). Build runtime first."; exit 1; }

# ====================================================================
# Test EAPPs
# ====================================================================

TEST_BINS := test-stack test-loop test-malloc test-long-nop \
             test-fibonacci test-fib-bench linux-memory linux-abi io-vector io-file \
             io-multiplex net-loopback net-pselect

.PHONY: tests $(TEST_BINS)

tests: $(TEST_BINS) native-crypto

# --- stack (assembly) ---
test-stack: $(BUILD_DIR)/test-stack
$(BUILD_DIR)/test-stack: $(TESTS_DIR)/stack/stack.S $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- loop (assembly) ---
test-loop: $(BUILD_DIR)/test-loop
$(BUILD_DIR)/test-loop: $(TESTS_DIR)/loop/loop.s $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- malloc ---
test-malloc: $(BUILD_DIR)/test-malloc
$(BUILD_DIR)/test-malloc: $(TESTS_DIR)/malloc/malloc.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- long-nop (generated assembly) ---
LONG_NOP_GEN := $(TESTS_DIR)/long-nop/generate_func.sh
LONG_NOP_ASM := $(BUILD_DIR)/add_long.S

test-long-nop: $(BUILD_DIR)/test-long-nop
$(LONG_NOP_ASM): $(LONG_NOP_GEN) | check-sdk
	@mkdir -p $(BUILD_DIR)
	bash $(LONG_NOP_GEN) 4097 $@ \
		$(TESTS_DIR)/long-nop/nop.s \
		$(TESTS_DIR)/long-nop/func_base.s \
		$(TESTS_DIR)/long-nop/nop.h

$(BUILD_DIR)/test-long-nop: $(TESTS_DIR)/long-nop/long-nop.S $(LONG_NOP_ASM) $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LONG_NOP_ASM) $(LIB_EAPP)

# --- fibonacci ---
test-fibonacci: $(BUILD_DIR)/test-fibonacci
$(BUILD_DIR)/test-fibonacci: $(TESTS_DIR)/fibonacci/fibonacci.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- fib-bench ---
test-fib-bench: $(BUILD_DIR)/test-fib-bench
$(BUILD_DIR)/test-fib-bench: $(TESTS_DIR)/fib-bench/fib-bench.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- Linux anonymous-memory lifecycle ---
linux-memory: $(BUILD_DIR)/linux-memory
$(BUILD_DIR)/linux-memory: $(TESTS_DIR)/linux-memory/linux-memory.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- Linux base ABI, time, random, uname, and signal-mask state ---
linux-abi: $(BUILD_DIR)/linux-abi
$(BUILD_DIR)/linux-abi: $(TESTS_DIR)/linux-abi/linux-abi.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- vectored IO producer and host edge-call path ---
io-vector: $(BUILD_DIR)/io-vector
$(BUILD_DIR)/io-vector: $(TESTS_DIR)/io-vector/io-vector.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- file IO lifecycle through the syscall producer ---
io-file: $(BUILD_DIR)/io-file
$(BUILD_DIR)/io-file: $(TESTS_DIR)/io-file/io-file.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- fcntl, pipe2, and epoll ABI/bounds regression ---
io-multiplex: $(BUILD_DIR)/io-multiplex
$(BUILD_DIR)/io-multiplex: $(TESTS_DIR)/io-multiplex/io-multiplex.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- UDP loopback through the network syscall producer ---
net-loopback: $(BUILD_DIR)/net-loopback
$(BUILD_DIR)/net-loopback: $(TESTS_DIR)/net-loopback/net-loopback.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- pselect6 descriptor and fd-set ABI through the network producer ---
net-pselect: $(BUILD_DIR)/net-pselect
$(BUILD_DIR)/net-pselect: $(TESTS_DIR)/net-pselect/net-pselect.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -o $@ $< $(LIB_EAPP)

# --- native attestation / sealing / random regression ---
NATIVE_CRYPTO_DIR := $(TESTS_DIR)/native-crypto
NATIVE_CRYPTO_EAPP := $(BUILD_DIR)/native-crypto
NATIVE_CRYPTO_RUNNER := $(BUILD_DIR)/native-crypto-runner

native-crypto: $(NATIVE_CRYPTO_EAPP)
$(NATIVE_CRYPTO_EAPP): $(NATIVE_CRYPTO_DIR)/native-crypto.c \
                       $(NATIVE_CRYPTO_DIR)/native-crypto-shared.h \
                       $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) $(EAPP_LDFLAGS) -I$(NATIVE_CRYPTO_DIR) \
		-o $@ $(NATIVE_CRYPTO_DIR)/native-crypto.c $(LIB_EAPP)

$(NATIVE_CRYPTO_RUNNER): $(NATIVE_CRYPTO_DIR)/native-crypto-runner.cpp \
                         $(NATIVE_CRYPTO_DIR)/native-crypto-fp-guard.S \
                         $(NATIVE_CRYPTO_DIR)/native-crypto-shared.h \
                         $(LIB_HOST) $(LIB_EDGE) $(LIB_VERIFIER) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(HOST_CFLAGS) $(HOST_LDFLAGS) -I$(NATIVE_CRYPTO_DIR) \
		-o $@ $(NATIVE_CRYPTO_DIR)/native-crypto-runner.cpp \
		$(NATIVE_CRYPTO_DIR)/native-crypto-fp-guard.S \
		$(LIB_HOST) $(LIB_EDGE) $(LIB_VERIFIER)

# ====================================================================
# Hello Example (custom linker: --entry=_start, no app.lds)
# ====================================================================

HELLO_DIR    := $(EXAMPLES_DIR)/hello
HELLO_EAPP   := $(BUILD_DIR)/hello
HELLO_RUNNER := $(BUILD_DIR)/hello-runner

.PHONY: hello

hello: $(HELLO_EAPP) $(HELLO_RUNNER)

$(HELLO_EAPP): $(HELLO_DIR)/eapp/hello.c $(LIB_EAPP) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CC) $(EAPP_CFLAGS) -static -nostdlib -Wl,--entry=_start -o $@ $< $(LIB_EAPP)

$(HELLO_RUNNER): $(HELLO_DIR)/host/host.cpp $(LIB_HOST) $(LIB_EDGE) $(LIB_VERIFIER) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(HOST_CFLAGS) $(HOST_LDFLAGS) -o $@ $< $(LIB_HOST) $(LIB_EDGE) $(LIB_VERIFIER)

# ====================================================================
# Host runners
# ====================================================================

TEST_HOST_SRC := $(TESTS_DIR)/test-runner.cpp $(TESTS_DIR)/edge_wrapper.cpp
TEST_RUNNER   := $(BUILD_DIR)/test-runner

$(TEST_RUNNER): $(TEST_HOST_SRC) $(LIB_HOST) $(LIB_EDGE) $(LIB_VERIFIER) | check-sdk
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(HOST_CFLAGS) $(HOST_LDFLAGS) -o $@ $(TEST_HOST_SRC) \
		$(LIB_HOST) $(LIB_EDGE) $(LIB_VERIFIER)

.PHONY: host
host: $(HELLO_RUNNER) $(TEST_RUNNER) $(NATIVE_CRYPTO_RUNNER)

# ====================================================================
# All
# ====================================================================

.PHONY: all
all: hello tests host

# ====================================================================
# Clean
# ====================================================================

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/test-* $(BUILD_DIR)/hello* \
	       $(BUILD_DIR)/native-crypto* $(BUILD_DIR)/linux-memory \
	       $(BUILD_DIR)/linux-abi \
	       $(BUILD_DIR)/io-vector $(BUILD_DIR)/io-file \
	       $(BUILD_DIR)/io-multiplex \
	       $(BUILD_DIR)/net-loopback $(BUILD_DIR)/net-pselect \
	       $(BUILD_DIR)/test-runner \
	       $(BUILD_DIR)/add_long.S

# ====================================================================
# Run (on target board)
# ====================================================================

# Run hello example
.PHONY: run-hello
run-hello: hello check-runtime
	$(HELLO_RUNNER) $(HELLO_EAPP) $(EYRIE_RT) $(LOADER_BIN)

# Run individual tests
define RUN_TEST
run-$(1): $(BUILD_DIR)/$(1) $(TEST_RUNNER) check-runtime
	$(TEST_RUNNER) $(BUILD_DIR)/$(1) $(EYRIE_RT) $(LOADER_BIN)
endef

$(foreach t,$(TEST_BINS),$(eval $(call RUN_TEST,$(t))))

.PHONY: run-native-crypto
run-native-crypto: $(NATIVE_CRYPTO_EAPP) $(NATIVE_CRYPTO_RUNNER) check-runtime
	$(NATIVE_CRYPTO_RUNNER) $(NATIVE_CRYPTO_EAPP) $(EYRIE_RT) $(LOADER_BIN)

# Run all tests
.PHONY: run-tests
run-tests: $(addprefix run-,$(TEST_BINS))

# ====================================================================
# Help
# ====================================================================

.PHONY: help
help:
	@echo "Zion Examples Build System"
	@echo ""
	@echo "  make all              - build hello + tests + host runners"
	@echo "  make hello            - build hello example + runner"
	@echo "  make tests            - build all test EAPPs"
	@echo "  make host             - build host runners"
	@echo ""
	@echo "  make test-stack       - build stack test only"
	@echo "  make test-fibonacci   - build fibonacci test only"
	@echo "  make test-malloc      - build malloc test only"
	@echo "  make test-loop        - build loop test only"
	@echo "  make test-long-nop    - build long-nop test only"
	@echo "  make test-fib-bench   - build fib-bench test only"
	@echo ""
	@echo "  make run-hello              - run hello example"
	@echo "  make run-test-stack         - run stack test"
	@echo "  make run-test-fibonacci     - run fibonacci test"
	@echo "  make run-test-malloc        - run malloc test"
	@echo "  make run-tests              - run all tests"
	@echo ""
	@echo "  make clean            - remove build artifacts"
	@echo ""
	@echo "  SDK_DIR=$(SDK_DIR)"
	@echo "  RUNTIME_DIR=$(RUNTIME_DIR)"
	@echo "  BUILD_DIR=$(BUILD_DIR)"
