#! /bin/bash

#HOST=192.168.20.80
HOST=localhost

ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -p 5009 guest@$HOST "sudo ./receiver.sh" &


echo "Done!"

