#!/bin/sh

set -e
set -u

ntests=0
npass=0
nfail=0
prog=$(basename $0)
timed_out=no
timeout_default=600

bail()
{
	echo "$@" 1>&2
	exit 1
}

child_handler()
{
	trap child_handler CHLD
	echo "$prog ignoring SIGCHLD." 1>&2
}

alarm_handler() 
{
	trap alarm_handler ALRM
	echo "$prog ignoring SIGALRM" 1>&2
}

usr1_handler()
{
	trap usr1_handler USR1
	echo "$prog timed out, canceling tests." 1>&2
	timed_out=yes
}

exit_handler()
{
	trap - EXIT HUP INT PIPE QUIT
	if [ ${tmpdir:-none} != none ]; then
		rm -rf $tmpdir
	fi
}

usage()
{
	echo "usage: ${prog} <hostname>" 1>&2
	exit 1
}

random_fail()
{
	if [ ${FABTSUITE_RANDOM_FAIL:-no} = no ]; then
		return 1
	fi

	r=$(dd if=/dev/random bs=4 count=1 2> /dev/null | od -A n -t u4)

	if [ $r -ge $((2**31)) ]; then
		return 0
	else
		return 1
	fi
}

print_flagset_line()
{
	flagset=$1
	read kw_realtime realtime
	if [ ${kw_realtime:-fail} = "fail" ]; then
		ntests=$(($ntests + 1))
		nfail=$(($nfail + 1))
		printf "%-31.31s %8s %-24s %s\\n" \
		    "$(echo $flagset | sed 's/,/ /g')" - - fail
		return 1
	fi
	read discard || true
	read discard || true
	read result || true
	if [ ${kw_realtime:-none} != "real" ]; then
		ntests=$(($ntests + 1))
		nfail=$(($nfail + 1))
		printf "%-31.31s %8s %-24s %s\\n" \
		    "$(echo $flagset | sed 's/,/ /g')" - - fail
		return 1
	fi
	ntests=$(($ntests + 1))
	if [ ${result:-none} = ok ]; then
		npass=$(($npass + 1))
	else
		nfail=$(($nfail + 1))
	fi
	if [ ${flagset} = "default" ]; then
		default_realtime=${realtime}
		printf "%-31.31s %8.2f %-24s %s\\n" "default" \
		    $realtime - $result
	else
		printf "%-31.31s %8.2f %-24.0f %s\\n" \
		    "$(echo $flagset | sed 's/,/ /g')" ${realtime} \
		    $(echo "2 k $realtime $default_realtime / 100 * p" | dc) \
		    $result
	fi
}

env_for_flagset()
{
	flagset=$1
	env=
	for flag in $(echo $flagset | sed 's/,/ /g'); do
		case $flag in
		cacheless)
			env="FI_MR_CACHE_MAX_SIZE=0 ${env}"
			;;
		contiguous)
			;;
		default)
			;;
		reregister)
			;;
		esac
	done
	echo $env
}

cmd_for_flagset()
{
	flagset=$1
	shift
	cmd="$@"
	for flag in $(echo $flagset | sed 's/,/ /g'); do
		case $flag in
		cacheless)
			;;
		contiguous)
			cmd="$cmd -g"
			;;
		default)
			;;
		reregister)
			cmd="$cmd -r"
			;;
		esac
	done
	echo $cmd
}

print_footer()
{
	cat<<FOOTER_EOF

key:

  parameters:
      default: register each RDMA buffer once, use scatter-gather RDMA 
      cacheless: env FI_MR_CACHE_MAX_SIZE=0, disable memory-registration cache
      contiguous: -g, RDMA conti(g)uous bytes, no scatter-gather
      reregister: -r, deregister/(r)eregister each RDMA buffer before reuse

  duration: elapsed real time in seconds

  duration/default: elapsed real time as a percentage of the duration
    measured with the default parameter set

${ntests} tests, ${npass} succeeded, ${nfail} failed
FOOTER_EOF
}

print_report()
{
	which=$1

	default_realtime=0

	cat<<HEADING_EOF
${which} parameter set          duration (s) duration/default (%)     result
--------------------------------------------------------------------------
HEADING_EOF

	# rely on flagset `default` to be first, *wince*.
	for flagset in $(eval echo \$${which}_flagset); do
		if [ -e $tmpdir/${which}.${flagset}.result ]; then
			print_flagset_line $flagset \
			    < $tmpdir/${which}.${flagset}.result || continue
		else
			print_flagset_line $flagset < /dev/null || continue
		fi
	done
}

if [ ${FABTSUITE_TIMEOUT_SET:-no} = no ]; then
	FABTSUITE_TIMEOUT_SET=yes timeout -s USR1 \
	    ${FABTSUITE_TIMEOUT:-$timeout_default} sh "$0" "$@"
	exit $?
fi

trap alarm_handler ALRM
trap usr1_handler USR1
trap exit_handler EXIT HUP INT PIPE QUIT
trap child_handler CHLD

if ! tmpdir=$(mktemp -d ${prog}.XXXXXX) ; then
	echo "could not create temporary directory, bailing." 1>&2
	exit 1
fi

if [ $# -ne 1 ]; then
	usage $0
fi

test_host=$1

generic_flagset="default reregister cacheless cacheless,reregister"
fget_flagset=$generic_flagset
fput_flagset="$generic_flagset contiguous contiguous,reregister"
fput_flagset="$fput_flagset contiguous,reregister,cacheless"

for flagset in $fget_flagset; do
	fgetenv=$(env_for_flagset $flagset)
	fgetcmd=$(cmd_for_flagset $flagset "./transfer/fget -b $test_host")

	{
		if env $fgetenv time -p $fgetcmd 2>&1 && ! random_fail && \
		   [ ${timed_out:-no} = no ] ; then
			echo ok
		else
			echo fail
		fi
	} > $tmpdir/fget.${flagset}.result &

	if ! ./transfer/fput $test_host; then
		echo fail > $tmpdir/fget.${flagset}.result
	fi

	while ! wait; do
		:
	done

	if [ ${timed_out:-no} = yes ]; then
		break
	fi
done

for flagset in $fput_flagset; do
	fputenv=$(env_for_flagset $flagset)
	fputcmd=$(cmd_for_flagset $flagset "./transfer/fput")
	./transfer/fget -b $test_host &
	if env $fputenv time -p $fputcmd $test_host 2>&1 && ! random_fail && \
	   [ ${timed_out:-no} = no ] ; then
		echo ok
	else
		echo fail
	fi > $tmpdir/fput.${flagset}.result

	while ! wait; do
		:
	done

	if [ ${timed_out:-no} = yes ]; then
		break
	fi
done

print_report fget

echo 

print_report fput

print_footer

if [ ${timed_out:-no} != no ]; then
	echo
	echo "*** A timeout occurred before all tests were run. ***" 
	exit 1
fi

exit 0
