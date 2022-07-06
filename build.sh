#!/bin/sh

set -e
set -u

for d in hlog transfer; do
	$d/build.sh
done
