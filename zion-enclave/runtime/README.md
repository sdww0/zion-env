# Zion Eyrie Modular Runtime

Eyrie only builds as part of the Zion [sdk](https://github.com/zion-enclave/zion-sdk).

We strongly encourage using the top-level [Zion](https://github.com/zion-enclave/zion) build process.

# Compatibility

| Name         | Version        |
|--------------|----------------|
| Zion SDK | v1.0 or higher |
| Zion SM  | v1.0 or higher |

# Building

## Building the Eyrie Runtime

Make sure you've properly set the environment variable `ZION_SDK_DIR` to point to the Zion SDK installation path.

Then, run `./build.sh [features]`.

## Running the tests

Make sure you checked out all submodules with `git submodule update --init`.

Then, run `make test`.

If a test fails and you'd like more detail, enter into `obj/test` and run the binary for the failed test. e.g. if `test_string` fails, run `obj/test/test_string`.

## Build options

See the sdk Makefile for feature selection.

# Contributing

The Eyrie Runtime is licensed under the 3-clause BSD license. See LICENSE for more details.

Before submitting a pull request to GitHub, make sure you format your code first.

```sh
make format
```
