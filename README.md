# Memory Allocator in C

A small Linux allocator compatible with the [Open Education Hub Memory Allocator assignment](https://github.com/open-education-hub/operating-systems/blob/main/content/assignments/memory-allocator/README.md).

Implements `os_malloc`, `os_calloc`, `os_realloc`, and `os_free` in `src/osmem.c`, with no calls to libc `malloc`, `calloc`, `realloc`, or `free`.

## Implementation

- Payload and metadata aligned to 8 bytes.
- `sbrk`-based heap, with **one 128 KiB preallocation** on the first heap allocation.
- `mmap`/`munmap` for large requests. `malloc` and `realloc` use a 128 KiB threshold; `calloc` uses the operating system's page size. Threshold comparison includes block metadata.
- Doubly linked, address-ordered heap-block list.
- **Best-fit** reuse after coalescing adjacent free blocks.
- Splitting only if the remaining space fits a block header plus one aligned payload.
- Expansion of a free last heap block; in-place `realloc` growth into adjacent free blocks or by extending the heap tail.
- `realloc` preserves the existing payload, refuses freed heap blocks, and moves mapped blocks without `mremap`.
- Checked size arithmetic, zero-sized requests, `calloc` multiplication overflow, and syscall return values.

The original assignment's syscall-trace checker is sensitive to the order of operations. The solution intentionally uses `sbrk` rather than `malloc` internally.

## Requirements

Linux (WSL2 Ubuntu works), GCC, GNU Make, Python 3, Git, and `ltrace`. `valgrind` is also required for the official checker in memory-check mode.

```bash
sudo apt update
sudo apt install -y build-essential git python3 ltrace valgrind unzip
```

## Local tests

From the repository root:

```bash
make test
```

The test executables cover alignment, preallocation, zero requests, overflow, `calloc` zeroing, the heap/mmap boundary, best-fit reuse, coalescing, block splitting, `realloc`, memory preservation, 18,000 randomized operations, and exact offsets from the upstream reference traces.

Local tests and official tests are **different** suites. The official suite is the grading reference.

## Official assignment tests

Run this command from the repository root:

```bash
./scripts/run-official-tests.sh
```

The script downloads the upstream assignment into `.upstream-tests/`, copies **only `src/osmem.c`** into its original `src/` folder, rebuilds the official library and snippets, and invokes `python3 run_tests.py -d` from the correct working directory. It writes the resulting score and failure messages to `official-tests.log`.

To include a Valgrind check:

```bash
./scripts/run-official-tests.sh -m
```

To inspect one test (for example):

```bash
./scripts/run-official-tests.sh test-realloc-coalesce -d
```

The upstream runner uses **`run_tests.py`** (with an underscore). Some assignment documentation mentions a hyphenated filename; follow the actual source filename.

The upstream test suite is not duplicated in this repository. It is downloaded unmodified when you run the script. Do not commit `.upstream-tests/`.

## Required screenshots

1. Open a WSL terminal in the repository root.
2. Run `make clean && make test` and capture the terminal output.
3. Run `./scripts/run-official-tests.sh -m` and capture the terminal with the official `Total: .../100` score visible. If the tests fail, inspect the log and fix them before submission.
4. On Windows, press **Win + Shift + S**, select the terminal, and save PNG files to `screenshots/local-tests.png` and `screenshots/official-tests.png`.
5. Commit and push the screenshots with the source files.

Screenshots must be real evidence from the **student's own machine**; this repository intentionally contains placeholders rather than generated screenshots.

## Publish to GitHub

Create an empty public `memory-allocator` repository on GitHub (do **not** initialize it with a README, as this directory already has one), then run:

```bash
git init
git add .
git commit -m "Implement Linux memory allocator"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/memory-allocator.git
git push -u origin main
```

Replace `YOUR_USERNAME` with your GitHub username. Submit the repository URL after your commit includes the screenshots.

## Repository layout

```text
memory-allocator/
├── src/
│   ├── osmem.c
│   └── Makefile
├── utils/
│   ├── osmem.h
│   └── block_meta.h
├── tests/
│   ├── test_allocator.c
│   └── test_reference_layout.c
├── scripts/
│   └── run-official-tests.sh
├── screenshots/
│   └── README.md
├── .gitignore
├── Makefile
└── README.md
```

### Limitations

This assignment targets single-threaded use of the allocator's `os_*` API. Like the original assignment, it assumes no unrelated allocator changes the program break while the heap is managed. It is not a drop-in, thread-safe replacement for libc `malloc`.
