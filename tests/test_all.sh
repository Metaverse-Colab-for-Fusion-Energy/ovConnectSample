#!/bin/bash

set -e

SCRIPT_DIR="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
echo Test script lives in ${SCRIPT_DIR}
ROOT_DIR=${SCRIPT_DIR}/..

echo Running script in ${ROOT_DIR}

pushd $ROOT_DIR > /dev/null

export PYTHON=${ROOT_DIR}/_build/target-deps/python/python
export PYTHONPATH=${ROOT_DIR}/source/pyHelloWorld

if [ ! -f ${PYTHON} ]; then
    echo "echo Python, USD, and Omniverse Client libraries are missing.  Run \"./repo.sh build --stage\" to retrieve them."
    popd
    exit
fi

${PYTHON} ./tests/test_all.py "$@"
popd > /dev/null
