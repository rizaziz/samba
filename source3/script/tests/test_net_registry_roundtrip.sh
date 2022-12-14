#!/bin/sh
#
# Blackbox test for net conf/registry roundtrips.
#
# Copyright (C) 2010 Gregor Beck <gbeck@sernet.de>
# Copyright (C) 2011 Michael Adam <obnox@samba.org>

if [ $# -lt 3 ]; then
	cat <<EOF
Usage: test_net_registry_roundtrip.sh SCRIPTDIR SERVERCONFFILE NET CONFIGURATION RPC
EOF
	exit 1
fi

SCRIPTDIR="$1"
SERVERCONFFILE="$2"
NET="$3"
CONFIGURATION="$4"
RPC="$5"

NET="$VALGRIND ${NET} $CONFIGURATION"

if test "x${RPC}" = "xrpc"; then
	NETCMD="${NET} -U${USERNAME}%${PASSWORD} -I ${SERVER_IP} rpc"
else
	NETCMD="${NET}"
fi

incdir=$(dirname $0)/../../../testprogs/blackbox
. $incdir/subunit.sh

failed=0

#
# List of parameters to skip when importing configuration files:
# They are forbidden in the registry and would lead import to fail.
#
SED_INVALID_PARAMS="{
s/state directory/;&/g
s/lock directory/;&/g
s/lock dir/;&/g
s/config backend/;&/g
s/include/;&/g
}"

REGPATH="HKLM\Software\Samba"

conf_roundtrip_step()
{
	echo "CMD: $*" >>$LOG
	"$@" 2>>$LOG
	RC=$?
	echo "RC: $RC" >>$LOG
	test "x$RC" = "x0" || {
		echo "ERROR: $* failed (RC=$RC)" | tee -a $LOG
	}
	return $RC
	#    echo -n .
}

LOGDIR_PREFIX="conf_roundtrip"

conf_roundtrip()
(
	DIR=$(mktemp -d ${PREFIX}/${LOGDIR_PREFIX}_XXXXXX)
	LOG=$DIR/log

	echo conf_roundtrip $1 >$LOG

	sed -e "$SED_INVALID_PARAMS" $1 >$DIR/conf_in

	conf_roundtrip_step $NETCMD conf drop
	test "x$?" = "x0" || {
		return 1
	}

	test -z "$($NETCMD conf list)" 2>>$LOG
	if [ "$?" = "1" ]; then
		echo "ERROR: conf drop failed" | tee -a $LOG
		return 1
	fi

	conf_roundtrip_step $NETCMD conf import $DIR/conf_in
	test "x$?" = "x0" || {
		return 1
	}

	conf_roundtrip_step $NETCMD conf list >$DIR/conf_exp
	test "x$?" = "x0" || {
		return 1
	}

	grep "\[global\]" $DIR/conf_exp >/dev/null 2>>$LOG
	if [ "$?" = "1" ]; then
		echo "ERROR: conf import => conf export failed" | tee -a $LOG
		return 1
	fi

	conf_roundtrip_step $NETCMD -d10 registry export $REGPATH $DIR/conf_exp.reg
	test "x$?" = "x0" || {
		return 1
	}

	conf_roundtrip_step $NETCMD conf drop
	test "x$?" = "x0" || {
		return 1
	}

	test -z "$($NETCMD conf list)" 2>>$LOG
	if [ "$?" = "1" ]; then
		echo "ERROR: conf drop failed" | tee -a $LOG
		return 1
	fi

	conf_roundtrip_step $NETCMD registry import $DIR/conf_exp.reg
	test "x$?" = "x0" || {
		return 1
	}

	conf_roundtrip_step $NETCMD conf list >$DIR/conf_out
	test "x$?" = "x0" || {
		return 1
	}

	diff -q $DIR/conf_out $DIR/conf_exp >>$LOG
	if [ "$?" = "1" ]; then
		echo "ERROR: registry import => conf export failed" | tee -a $LOG
		return 1
	fi

	conf_roundtrip_step $NETCMD registry export $REGPATH $DIR/conf_out.reg
	test "x$?" = "x0" || {
		return 1
	}

	diff -q $DIR/conf_out.reg $DIR/conf_exp.reg >>$LOG
	if [ "$?" = "1" ]; then
		echo "Error: registry import => registry export failed" | tee -a $LOG
		return 1
	fi
	rm -r $DIR
)

CONF_FILES=$SERVERCONFFILE

# remove old logs:
for OLDDIR in $(find ${PREFIX} -type d -name "${LOGDIR_PREFIX}_*"); do
	echo "removing old directory ${OLDDIR}"
	rm -rf ${OLDDIR}
done

for conf_file in $CONF_FILES; do
	testit "conf_roundtrip $conf_file" \
		conf_roundtrip $conf_file ||
		failed=$(expr $failed + 1)
done

testok $0 $failed
