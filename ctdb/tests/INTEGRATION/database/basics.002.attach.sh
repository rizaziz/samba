#!/usr/bin/env bash

# Verify that databases are attached a node joins the cluster:
# 1. Shut down CTDB on one node
# 2. Attach test databases
# 3. Check that databases are attached on all up nodes
# 4. Start CTDB on the node where it is shut down
# 5. Verify that the test databases are attached on this node
# 6. Restart one of the nodes
# 7. Verify that the test databases are attached on this node

. "${TEST_SCRIPTS_DIR}/integration.bash"

set -e

ctdb_test_init

######################################################################

try_command_on_node 0 "$CTDB listnodes -X | wc -l"
numnodes="$out"
lastnode=$(( numnodes - 1 ))

######################################################################

# Confirm that the database is attached with appropriate flags
check_db_once ()
{
	local pnn="$1"
	local db="$2"

	try_command_on_node "$pnn" $CTDB getdbmap
	if grep -qF "name:${db}" "$outfile" >/dev/null ; then
		return 0
	else
		return 1
	fi
}

check_db ()
{
	local pnn="$1"
	local db="$2"
	local flag="$3"

	local flags

	echo "Waiting until database ${db} is attached on node ${pnn}"
	wait_until 10 check_db_once "$pnn" "$db"

	flags=$(awk -v db="$db" '$2 == "name:" db {print $4}' "$outfile")
	if [ "$flags" = "$flag" ]; then
		echo "GOOD: db ${db} attached on node ${pnn} with flag $flag"
	else
		echo "BAD: db ${db} attached on node ${pnn} with wrong flag"
		cat "$outfile"
		exit 1
	fi
}

######################################################################

testdb1="test_volatile.tdb"
testdb2="test_persistent.tdb"
testdb3="test_replicated.tdb"

test_node="0"

echo "Shutting down node $test_node"
ctdb_nodes_stop "$test_node"
sleep 1
wait_until_node_has_status 1 recovered
try_command_on_node -v 1 $CTDB status

echo "Create test databases"
try_command_on_node 1 $CTDB attach "$testdb1"
try_command_on_node 1 $CTDB attach "$testdb2" persistent
try_command_on_node 1 $CTDB attach "$testdb3" replicated

echo
echo "Checking if database is attached with correct flags"
for node in $(seq 0 $lastnode) ; do
    if [ $node -ne $test_node ] ; then
	check_db $node $testdb1 ""
	check_db $node $testdb2 PERSISTENT
	check_db $node $testdb3 REPLICATED
    fi
done

######################################################################

echo
echo "Start node $test_node"
ctdb_nodes_start "$test_node"
sleep 1
wait_until_ready

echo
echo "Checking if database is attached with correct flags"
check_db $test_node $testdb1 ""
check_db $test_node $testdb2 PERSISTENT
check_db $test_node $testdb3 REPLICATED

######################################################################

echo
echo "Restarting node $test_node"
ctdb_nodes_restart "$test_node"
sleep 1
wait_until_ready

echo
echo "Checking if database is attached with correct flags"
check_db $test_node $testdb1 ""
check_db $test_node $testdb2 PERSISTENT
check_db $test_node $testdb3 REPLICATED
