#!/usr/bin/env bash

usage(){
    echo "Usage: ${0} <writefile> <writestr>"
    echo "where"
    echo "  writefile: file to write to (created if it doesn't exist)"
    echo "  writestr: content to write (clobbers existing content)"
}

if [[ $# -lt 2 ]]
then
    echo "Missing parameters"
    usage
    exit 1
fi

writefile=$1
writestr=$2

mkdir -p "$(dirname "${writefile}")"
touch "${writefile}"
if [[ ! -f "${writefile}" ]]
then
    echo "Could not create file '${writefile}'"
    exit 1
fi
echo "${writestr}" > "${writefile}"
