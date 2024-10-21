#!/bin/bash
# Do not inherit the environment, it's configured for development and we want to configure to build

script_dir="$(dirname "$(realpath "$0")")"
yocto_prj_root="$script_dir/../../../../.." # path to the project root from the 'modify' workspace
pushd "$yocto_prj_root" || exit 1
source "poky/oe-init-build-env"
bitbake -c clean aesdchar
bitbake aesdchar
pushd "$script_dir" || exit 1 # go to the 'modify' workspace
make_command="$(grep -Pho -m1 '(?<=NOTE: )make .*$' oe-logs/log.do_compile)"
# quote the CC arguments
make_command="${make_command/CC/\"CC}"
make_command="${make_command/ LD/\" LD}"
sh -c "bear -- $make_command"
popd || exit 1
popd || exit 1
