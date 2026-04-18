# Truecolor checker

This is a small program to check if your terminal supports truecolor or not using some terminal sequence such as XTGETTCAP and DECRQSS SGR.
If your terminal supports truecolor, this program exits with 0, otherwise exits with 1.

## Requirement

- C++ compiler that supports C++20.
- Some POSIX headers.

## Build and run

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
