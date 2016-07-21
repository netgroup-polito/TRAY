#! /bin/bash

HOST=localhost

start_ovs="ssh -t -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
	stack@$HOST 											\
	'cd /home/stack/Mauricio/directvm2vm/scripts/;			\
	sudo ./0_start_ovs.sh;									\
	exec $SHELL'"

start_name_resolver="ssh -t -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
		stack@$HOST 										\
		'cd /home/stack/Mauricio/directvm2vm/scripts/;		\
		sudo ./1_start_name_resolver.sh;					\
		exec $SHELL'"

start_orchestrator="ssh -t -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
		stack@$HOST 										\
		'cd /home/stack/Mauricio/directvm2vm/scripts/;		\
		sleep 30 && sudo ./2_start_orchestrator.sh; 					\
		exec $SHELL'"

gnome-terminal  --tab -e "$start_ovs" 			\
				--tab -e "$start_name_resolver" \
				--tab -e "$start_orchestrator"

echo "Done!"
