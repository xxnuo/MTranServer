#!/bin/bash

echo "--- Building from Source ---"

make build-ui
make build

echo "Build complete. Binary is at ./dist/mtranserver-*"
