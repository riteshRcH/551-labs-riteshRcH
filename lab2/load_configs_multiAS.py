from subprocess import Popen, PIPE, STDOUT
import sys

routers_config = {
"SEAT": {"host": "4.109.0.2/24", "salt": ["4.0.12.2/24", "913"], "losa": ["4.0.13.2/24", "1342"], "west": "5.0.1.1/24"},
"SALT": {"host": "4.107.0.2/24", "seat": ["4.0.12.1/24", "913"], "kans": ["4.0.9.2/24", "1330"], "losa": ["4.0.11.1/24", "1303"]},
"KANS": {"host": "4.105.0.2/24", "salt": ["4.0.9.1/24", "1330"], "chic": ["4.0.6.2/24", "690"], "hous": ["4.0.8.1/24", "818"]},
"CHIC": {"host": "4.102.0.2/24", "newy": ["4.0.1.2/24", "1000"], "wash": ["4.0.2.2/24", "905"], "atla": ["4.0.3.2/24", "1045"], "kans": ["4.0.6.1/24", "690"]},
"NEWY": {"host": "4.101.0.2/24", "chic": ["4.0.1.1/24", "1000"], "wash": ["4.0.4.1/24", "277"], "east": "6.0.1.1/24"},
"WASH": {"host": "4.103.0.2/24", "chic": ["4.0.2.1/24", "905"], "newy": ["4.0.4.2/24", "277"], "atla": ["4.0.5.1/24", "700"]},
"ATLA": {"host": "4.104.0.2/24", "chic": ["4.0.3.1/24", "1045"], "wash": ["4.0.5.2/24", "700"], "hous": ["4.0.7.1/24", "1385"]},
"HOUS": {"host": "4.106.0.2/24", "kans": ["4.0.8.2/24", "818"], "atla": ["4.0.7.2/24", "1385"], "losa": ["4.0.10.1/24", "1705"]},
"LOSA": {"host": "4.108.0.2/24", "salt": ["4.0.11.2/24", "1303"], "hous": ["4.0.10.2/24", "1705"], "seat": ["4.0.13.1/24", "1342"]},
"east": {"newy": "6.0.1.2/24", "server1": "6.0.2.1/24", "server2": "6.0.3.1/24"},
"west": {"seat": "5.0.1.2/24", "sr": "5.0.2.1/24"},
}

asns = {"i2": "4", "west": "5", "east": "6"}

router_asn_belongings = {"SEAT": "i2", "SALT": "i2", "KANS": "i2", "CHIC": "i2", "NEWY": "i2", "WASH": "i2", "ATLA": "i2", "HOUS": "i2", "LOSA": "i2", "west": "west", "sr": "west", "east": "east"}

bgp_loopback_addresses = {
	"NEWY": "4.101.0.2",
	"CHIC": "4.102.0.2",
	"WASH": "4.103.0.2",
	"ATLA": "4.104.0.2",
	"KANS": "4.105.0.2",
	"HOUS": "4.106.0.2",
	"SALT": "4.107.0.2",
	"LOSA": "4.108.0.2",
	"SEAT": "4.109.0.2",
	"west": "5.0.1.2",
	"east": "6.0.1.2"
}

ibgp_peers = set(["NEWY", "CHIC", "WASH", "ATLA", "KANS", "HOUS", "SALT", "LOSA", "SEAT"])

networks = ["4.109.0.0/24", "4.0.12.0/24", "4.107.0.0/24", "4.0.9.0/24", "4.105.0.0/24", "4.0.6.0/24", "4.102.0.0/24", "4.0.1.0/24", "4.101.0.0/24", "4.0.4.0/24", "4.103.0.0/24", "4.0.5.0/24", "4.104.0.0/24", "4.0.7.0/24", "4.106.0.0/24", "4.0.10.0/24", "4.108.0.0/24", "4.0.13.0/24", "4.0.11.0/24", "4.0.8.0/24", "4.0.3.0/24", "4.0.2.0/24"]

for router in routers_config:
	print("Configuring "+router+" interfaces....")

	for interface in routers_config[router]:
		if interface == "host" or router == "east" or router == "west" or (router == "NEWY" and interface == "east") or (router == "SEAT" and interface == "west"):
			vtysh_cmd = b" vtysh -c 'conf t\ninterface "+interface+"\nip address "+routers_config[router][interface]+"'"
		else:
			vtysh_cmd = b" vtysh -c 'conf t\ninterface "+interface+"\nip address "+routers_config[router][interface][0]+"\nip ospf cost "+routers_config[router][interface][1]+"'"
		
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
	print("Configuring bgp on "+router+"....")

	if router in ibgp_peers:
		if router == "NEWY":
			vtysh_cmd = b" vtysh -c 'conf t\nrouter bgp "+asns[router_asn_belongings[router]]+'\n'+'\n'.join(list(map(lambda x:"neighbor "+bgp_loopback_addresses[x]+" remote-as "+asns[router_asn_belongings[router]], ibgp_peers-{router})))+"\nneighbor "+bgp_loopback_addresses["east"]+" remote-as "+asns["east"]+"'"
		elif router == "SEAT":
			vtysh_cmd = b" vtysh -c 'conf t\nrouter bgp "+asns[router_asn_belongings[router]]+'\n'+'\n'.join(list(map(lambda x:"neighbor "+bgp_loopback_addresses[x]+" remote-as "+asns[router_asn_belongings[router]], ibgp_peers-{router})))+"\nneighbor "+bgp_loopback_addresses["west"]+" remote-as "+asns["west"]+"'"
		else:
			vtysh_cmd = b" vtysh -c 'conf t\nrouter bgp "+asns[router_asn_belongings[router]]+'\n'+'\n'.join(list(map(lambda x:"neighbor "+bgp_loopback_addresses[x]+" remote-as "+asns[router_asn_belongings[router]], ibgp_peers-{router})))+"'"
	elif router == "east":
		vtysh_cmd = b" vtysh -c 'conf t\nrouter bgp "+asns[router_asn_belongings[router]]+"\nneighbor "+bgp_loopback_addresses["NEWY"]+" remote-as "+asns[router_asn_belongings["NEWY"]]+"'"
	elif router == "west":
		vtysh_cmd = b" vtysh -c 'conf t\nrouter bgp "+asns[router_asn_belongings[router]]+"\nneighbor "+bgp_loopback_addresses["SEAT"]+" remote-as "+asns[router_asn_belongings["SEAT"]]+"'"
	sys.stdout.write(vtysh_cmd+" ==> exit code ")

	p = Popen(['./go_to.sh', router], stdout=PIPE, stdin=PIPE, stderr=STDOUT)
	p.communicate(input=vtysh_cmd)[0]
	print(p.returncode)

	print	