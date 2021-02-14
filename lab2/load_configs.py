from subprocess import Popen, PIPE, STDOUT
import sys

routers_config = {
"SEAT": {"host": "4.109.0.2/24", "salt": ["4.0.12.2/24", "913"], "losa": ["4.0.13.2/24", "1342"]},
"SALT": {"host": "4.107.0.2/24", "seat": ["4.0.12.1/24", "913"], "kans": ["4.0.9.2/24", "1330"], "losa": ["4.0.11.1/24", "1303"]},
"KANS": {"host": "4.105.0.2/24", "salt": ["4.0.9.1/24", "1330"], "chic": ["4.0.6.2/24", "690"], "hous": ["4.0.8.1/24", "818"]},
"CHIC": {"host": "4.102.0.2/24", "newy": ["4.0.1.2/24", "1000"], "wash": ["4.0.2.2/24", "905"], "atla": ["4.0.3.2/24", "1045"], "kans": ["4.0.6.1/24", "690"]},
"NEWY": {"host": "4.101.0.2/24", "chic": ["4.0.1.1/24", "1000"], "wash": ["4.0.4.1/24", "277"]},
"WASH": {"host": "4.103.0.2/24", "chic": ["4.0.2.1/24", "905"], "newy": ["4.0.4.2/24", "277"], "atla": ["4.0.5.1/24", "700"]},
"ATLA": {"host": "4.104.0.2/24", "chic": ["4.0.3.1/24", "1045"], "wash": ["4.0.5.2/24", "700"], "hous": ["4.0.7.1/24", "1385"]},
"HOUS": {"host": "4.106.0.2/24", "kans": ["4.0.8.2/24", "818"], "atla": ["4.0.7.2/24", "1385"], "losa": ["4.0.10.1/24", "1705"]},
"LOSA": {"host": "4.108.0.2/24", "salt": ["4.0.11.2/24", "1303"], "hous": ["4.0.10.2/24", "1705"], "seat": ["4.0.13.1/24", "1342"]}
}

networks = ["4.109.0.0/24", "4.0.12.0/24", "4.107.0.0/24", "4.0.9.0/24", "4.105.0.0/24", "4.0.6.0/24", "4.102.0.0/24", "4.0.1.0/24", "4.101.0.0/24", "4.0.4.0/24", "4.103.0.0/24", "4.0.5.0/24", "4.104.0.0/24", "4.0.7.0/24", "4.106.0.0/24", "4.0.10.0/24", "4.108.0.0/24", "4.0.13.0/24", "4.0.11.0/24", "4.0.8.0/24", "4.0.3.0/24", "4.0.2.0/24"]

for router in routers_config:
	print("Configuring "+router+" interfaces....")

	for interface in routers_config[router]:
		vtysh_cmd = b" vtysh -c 'conf t\ninterface "+interface+"\nip address "+routers_config[router][interface][0]+"'"
		sys.stdout.write(vtysh_cmd+" ==> exit code ")

		p = Popen(['./go_to.sh', router], stdout=PIPE, stdin=PIPE, stderr=STDOUT)
		p.communicate(input=vtysh_cmd)[0]
		print(p.returncode)

	print

print("########################################################################################################")

for router in routers_config:
	print("Configuring ospf on "+router+"....")

	vtysh_cmd = b" vtysh -c 'conf t\nrouter ospf\n"+'\n'.join(list(map(lambda x:"network "+x+" area 0", networks)))+"'"
	sys.stdout.write(vtysh_cmd+" ==> exit code ")

	p = Popen(['./go_to.sh', router], stdout=PIPE, stdin=PIPE, stderr=STDOUT)
	p.communicate(input=vtysh_cmd)[0]
	print(p.returncode)

	print	

print("########################################################################################################")

for router in routers_config:
	print("Configuring ospf link weight on "+router+"....")

	for interface in routers_config[router]:
		if interface != "host":
			vtysh_cmd = b" vtysh -c 'conf t\ninterface "+interface+"\nip ospf cost "+routers_config[router][interface][1]+"'"
			sys.stdout.write(vtysh_cmd+" ==> exit code ")

			p = Popen(['./go_to.sh', router], stdout=PIPE, stdin=PIPE, stderr=STDOUT)
			p.communicate(input=vtysh_cmd)[0]
			print(p.returncode)

		print