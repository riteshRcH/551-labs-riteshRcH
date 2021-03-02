from subprocess import Popen, PIPE, STDOUT
import sys

hosts_config = {
"SEAT-host": ["seat", "4.109.0.1/24", "4.109.0.2"],
"SALT-host": ["salt", "4.107.0.1/24", "4.107.0.2"],
"KANS-host": ["kans", "4.105.0.1/24", "4.105.0.2"],
"CHIC-host": ["chic", "4.102.0.1/24", "4.102.0.2"],
"NEWY-host": ["newy", "4.101.0.1/24", "4.101.0.2"],
"WASH-host": ["wash", "4.103.0.1/24", "4.103.0.2"],
"ATLA-host": ["atla", "4.104.0.1/24", "4.104.0.2"],
"HOUS-host": ["hous", "4.106.0.1/24", "4.106.0.2"],
"LOSA-host": ["losa", "4.108.0.1/24", "4.108.0.2"],
"client": ["client-eth0", "5.1.1.2/24", "5.1.1.1"],
"server1": ["east", "6.0.2.2/24", "6.0.2.1"],
"server2": ["east", "6.0.3.2/24", "6.0.3.1"]
}

for host in hosts_config:
	print("Configuring "+host+"....")

	ifconfig = b"sudo ifconfig "+hosts_config[host][0]+" "+hosts_config[host][1]+" up"
	sys.stdout.write(ifconfig+" ==> exit code ")

	p = Popen(['./go_to.sh', host], stdout=PIPE, stdin=PIPE, stderr=STDOUT)
	p.communicate(input=ifconfig)[0]
	print(p.returncode)

	default_gateway = b"sudo route add default gw "+hosts_config[host][2]+" "+hosts_config[host][0]
	sys.stdout.write(default_gateway+" ==> exit code ")

	p = Popen(['./go_to.sh', host], stdout=PIPE, stdin=PIPE, stderr=STDOUT)
	p.communicate(input=default_gateway)[0]
	print(p.returncode)

	print("########################################################################################################")