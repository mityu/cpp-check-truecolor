# Truecolor checker

This is a small program to check if your terminal supports truecolor or not using some terminal sequence such as XTGETTCAP and DECRQSS SGR.
If your terminal supports truecolor, this program exits with 0, otherwise exits with 1.

## Sample Use Case

- Automatic truecolor detection on SSH login.

```sh
# Write this in your .bash_profile

if [[ -n "${SSH_TTY:-}" ]] && [[ -z "${COLORTERM:-}" ]] && \
    type check-truecolor > /dev/null && check-truecolor
then
    export COLORTERM=truecolor
fi
```

## Build Requirement

- C++ compiler that supports C++20.
- Some POSIX headers.

## Build and Run

### Basic way

```sh
make
./check-truecolor
```

or

```sh
make run
```

### For Nix users

```sh
nix build .
./result/bin/check-truecolor
```

or

```sh
nix run github:mityu#cpp-check-truecolor
```

## Run Tests

Additionaly, Deno is required to run tests: https://github.com/denoland/deno

```sh
make test
```

or to test binary built by `nix build .`

```sh
CHECK_TRUECOLOR_EXECUTABLE=./result/bin/check-truecolor make test
```
