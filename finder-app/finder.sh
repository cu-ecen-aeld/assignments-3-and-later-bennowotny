#!/usr/bin/env bash

usage(){
    echo "Usage: ${0} <filesdir> <searchstr>"
    echo "where"
    echo "  filesdir: directory to search"
    echo "  searchstr: string to search for"
}

if [[ $# -lt 2 ]]
then
    echo "Missing parameters"
    usage
    exit 1
fi

if [[ ! -d $1 ]]
then
    echo "'${1}' is not a directory"
    usage
    exit 1
fi

filesdir=$1
searchstr=$2

files=$(find "${filesdir}" -type f)
num_files=$(echo "${files}" | wc -l)
num_matched_lines=$(echo "${files}" | xargs grep "${searchstr}" | wc -l )

echo "The number of files are ${num_files} and the number of matching lines are ${num_matched_lines}"
