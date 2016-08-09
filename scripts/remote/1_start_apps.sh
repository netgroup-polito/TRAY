#! /bin/bash

HOST=demo

start_receiver1="ssh -t -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
	-p 5009 guest@$HOST 'sudo ./debug_receiver.sh;exec $SHELL'"

start_receiver2="ssh -t -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
	-p 5010 guest@$HOST 'sudo ./debug_receiver.sh;exec $SHELL'"

start_sender="ssh -t -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
	-p 5000 guest@$HOST 'sudo ./debug_sender.sh;exec $SHELL'"

gnome-terminal  --tab -e "$start_receiver1" 	\
				--tab -e "$start_receiver2" \
				--tab -e "$start_sender"

echo "Done!"
