#! /bin/bash

HOST=localhost

start_receiver1="ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
	-p 5009 guest@$HOST 'sudo ./receiver.sh'"

start_receiver2="ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
	-p 5010 guest@$HOST 'sudo ./receiver.sh'"

start_sender="ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
	-p 5000 guest@$HOST 'sudo ./sender.sh'"

gnome-terminal  --tab -e "$start_receiver1" 	\
				--tab -e "$start_receiver1" \
				--tab -e "$start_sender"

echo "Done!"
