#! /usr/bin/python

import requests
import json
import sys

graph = '../../conf_files/nf_fg_off.json';

if len(sys.argv) >= 2:
	if sys.argv[1] == 'on':
		graph = '../../conf_files/nf_fg_on.json';

login_url = "http://130.192.225.151:8080/login"
graph_url = "http://130.192.225.151:8080/NF-FG/myGraph"

headers_login = {'Content-Type': 'application/json'}
payload = {'username':'admin','password':'1234'}
r = requests.post(login_url, data=json.dumps(payload), headers=headers_login)

token = r.text;
#print('token is: ' + token)

with open(graph, 'r') as myfile:
	data_file = myfile.read()
	headers_deploy = {'Content-Type': 'application/json', 'X-Auth-Token': token}
	r = requests.put(graph_url, data=data_file, headers=headers_deploy)
	if r.status_code == 201:
		print("graph deployed correctly")
	else:
		print("error deploying graph")
