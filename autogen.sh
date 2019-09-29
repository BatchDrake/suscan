#!/bin/bash

STDOUT_LOG=autogen-stdout.log
STDERR_LOG=autogen-stderr.log

function alternatives()
{
    VARIABLE=$1
    shift
    
    for i in $@; do
	if which $i 2> /dev/null > /dev/null; then
	   export $VARIABLE=$i
	   return 0
	fi
    done

    echo -e '(!) No alternative for \033[1m'$VARIABLE'\033[0m has been found.'
    echo -e '(!) Provided alternatives were: '$@
    exit 1
}

function try_to_run()
{
    echo -en '(*) Running \033[1m'$@'\033[0m... '
    if "$@" > $STDOUT_LOG 2> $STDERR_LOG; then
	echo -e '\033[1;32mOK!\033[0m'
	rm $STDOUT_LOG 2> /dev/null
	rm $STDERR_LOG 2> /dev/null
    else
	echo -e '\033[1;31mFAILED!\033[0m'
	echo "    See $STDOUT_LOG and $STDERR_LOG for details"
	exit 1;
   fi
}

alternatives LIBTOOLIZE glibtoolize libtoolize

try_to_run aclocal
try_to_run autoheader
try_to_run $LIBTOOLIZE
try_to_run automake --add-missing
try_to_run autoconf

exit 0
