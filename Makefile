CC=gcc
CFLAGS=-Wall -Wextra -O2

MEDSANDBOX_LDFLAGS=-lseccomp -lcap -lutil -ljansson
QSOFA_LDFLAGS=-ljansson

CLINICAL_DIR=clinical_plugins
SAMPLES_DIR=samples
OUT_DIR=out

TARGETS=medsandbox fake_medical_app memory_hog file_writer network_test ptrace_test fork_bomb $(CLINICAL_DIR)/qsofa

all: dirs $(TARGETS)

dirs:
	mkdir -p $(CLINICAL_DIR) $(SAMPLES_DIR) $(OUT_DIR) logs

medsandbox: medsandbox.c syscall_names.h
	$(CC) $(CFLAGS) -o medsandbox medsandbox.c $(MEDSANDBOX_LDFLAGS)

fake_medical_app: fake_medical_app.c
	$(CC) $(CFLAGS) -o fake_medical_app fake_medical_app.c

memory_hog: memory_hog.c
	$(CC) $(CFLAGS) -o memory_hog memory_hog.c

file_writer: file_writer.c
	$(CC) $(CFLAGS) -o file_writer file_writer.c

network_test: network_test.c
	$(CC) $(CFLAGS) -o network_test network_test.c

ptrace_test: ptrace_test.c
	$(CC) $(CFLAGS) -o ptrace_test ptrace_test.c

fork_bomb: fork_bomb.c
	$(CC) $(CFLAGS) -o fork_bomb fork_bomb.c

$(CLINICAL_DIR)/qsofa: $(CLINICAL_DIR)/qsofa.c
	$(CC) $(CFLAGS) -o $(CLINICAL_DIR)/qsofa $(CLINICAL_DIR)/qsofa.c $(QSOFA_LDFLAGS)

demo-qsofa: all
	./medsandbox --clinical --stdout $(OUT_DIR)/qsofa_result.json --stderr $(OUT_DIR)/qsofa_error.txt ./$(CLINICAL_DIR)/qsofa $(SAMPLES_DIR)/patient_qsofa.fhir.json
	cat $(OUT_DIR)/qsofa_result.json

demo-security: all
	./medsandbox --profile strict ./network_test || true
	./medsandbox -m 50 ./memory_hog || true
	./medsandbox -f 1 ./file_writer || true
	./medsandbox --profile strict ./ptrace_test || true

demo-all: demo-qsofa demo-security

test: all
	./tests/test_qsofa.sh
	./tests/test_network_block.sh
	./tests/test_memory_limit.sh
	./tests/test_file_limit.sh
	./tests/test_ptrace_block.sh

clean:
	rm -f medsandbox fake_medical_app memory_hog file_writer network_test ptrace_test fork_bomb
	rm -f $(CLINICAL_DIR)/qsofa
	rm -f big_output.txt
	rm -f $(OUT_DIR)/*.json $(OUT_DIR)/*.txt

.PHONY: all dirs demo-qsofa demo-security demo-all test clean
