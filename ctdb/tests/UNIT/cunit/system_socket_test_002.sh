#!/bin/sh

. "${TEST_SCRIPTS_DIR}/unit.sh"

tcp_test ()
{
	unit_test system_socket_test tcp "$@"
}

test_case "ACK, IPv4, seq# 0, ack# 0"
ok <<EOF
000000 45 00 00 08 00 00 00 00 ff 06 00 00 c0 a8 01 19
000010 c0 a8 02 4b 01 bd d4 31 00 00 00 00 00 00 00 00
000020 50 10 04 d2 50 5f 00 00
000028
EOF
tcp_test "192.168.1.25:445" "192.168.2.75:54321" 0 0 0

test_case "RST, IPv4, seq# 0, ack# 0"
ok <<EOF
000000 45 00 00 08 00 00 00 00 ff 06 00 00 c0 a8 01 19
000010 c0 a8 02 4b 01 bd d4 31 00 00 00 00 00 00 00 00
000020 50 14 04 d2 50 5b 00 00
000028
EOF
tcp_test "192.168.1.25:445" "192.168.2.75:54321" 0 0 1

test_case "RST, IPv4, seq# 12345, ack# 23456"
ok <<EOF
000000 45 00 00 08 00 00 00 00 ff 06 00 00 c0 a8 01 19
000010 c0 a8 02 4b 01 bd d4 31 39 30 00 00 a0 5b 00 00
000020 50 14 04 d2 76 cf 00 00
000028
EOF
tcp_test "192.168.1.25:445" "192.168.2.75:54321" 12345 23456 1

test_case "ACK, IPv6, seq# 0, ack# 0"
ok <<EOF
000000 60 00 00 00 00 14 06 40 fe 80 00 00 00 00 00 00
000010 6a f7 28 ff fe fa d1 36 fe 80 00 00 00 00 00 00
000020 6a f7 28 ff fe fb d1 37 01 bd d4 31 00 00 00 00
000030 00 00 00 00 50 10 04 d2 0f c0 00 00
00003c
EOF
tcp_test "[fe80::6af7:28ff:fefa:d136]:445" \
	 "[fe80::6af7:28ff:fefb:d137]:54321" 0 0 0

test_case "RST, IPv6, seq# 0, ack# 0"
ok <<EOF
000000 60 00 00 00 00 14 06 40 fe 80 00 00 00 00 00 00
000010 6a f7 28 ff fe fa d1 36 fe 80 00 00 00 00 00 00
000020 6a f7 28 ff fe fb d1 37 01 bd d4 31 00 00 00 00
000030 00 00 00 00 50 14 04 d2 0f bc 00 00
00003c
EOF
tcp_test "[fe80::6af7:28ff:fefa:d136]:445" \
	 "[fe80::6af7:28ff:fefb:d137]:54321" 0 0 1

test_case "RST, IPv6, seq# 12345, ack# 23456"
ok <<EOF
000000 60 00 00 00 00 14 06 40 fe 80 00 00 00 00 00 00
000010 6a f7 28 ff fe fa d1 36 fe 80 00 00 00 00 00 00
000020 6a f7 28 ff fe fb d1 37 01 bd d4 31 39 30 00 00
000030 a0 5b 00 00 50 14 04 d2 36 30 00 00
00003c
EOF
tcp_test "[fe80::6af7:28ff:fefa:d136]:445" \
	 "[fe80::6af7:28ff:fefb:d137]:54321" 12345 23456 1
