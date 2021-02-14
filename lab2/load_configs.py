from subprocess import Popen, PIPE, STDOUT
import sys

routers_config = {
"SEAT": {"host": "4.109.0.2/24", "salt": "4.0.12.2/24", "losa": "4.0.13.2/24"},
"SALT": {"host": "4.107.0.2/24", "seat": "4.0.12.1/24", "kans": "4.0.9.2/24", "losa": "4.0.11.1/24"},
"KANS": {"host": "4.105.0.2/24", "salt": "4.0.9.1/24", "chic": "4.0.6.2/24", "hous": "4.0.8.1/24"},
"CHIC": {"host": "4.102.0.2/24", "newy": "4.0.1.2/24", "wash": "4.0.2.2/24", "atla": "4.0.3.2/24", "kans": "4.0.6.1/24"},
"NEWY": {"host": "4.101.0.2/24", "chic": "4.0.1.1/24", "wash": "4.0.4.1/24"},
"WASH": {"host": "4.103.0.2/24", "chic": "4.0.2.1/24", "newy": "4.0.4.2/24", "atla": "4.0.5.1/24"},
"ATLA": {"host": "4.104.0.2/24", "chic": "4.0.3.1/24", "wash": "4.0.5.2/24", "hous": "4.0.7.1/24"},
"HOUS": {"host": "4.106.0.2/24", "kans": "4.0.8.2/24", "atla": "4.0.7.2/24", "losa": "4.0.10.1/24"},
"LOSA": {"host": "4.108.0.2/24", "salt": "4.0.11.2/24", "hous": "4.0.10.2/24", "seat": "4.0.13.1/24"}
}

for router in routers_config:
	print("Configuring "+router+"....")

	for interface in routers_config[router]:
		vtysh_cmd = b" vtysh -c 'conf t\ninterface "+interface+"\nip address "+routers_config[router][interface]+"'"
		sys.stdout.write(vtysh_cmd+" ==> exit code ")

		p = Popen(['./go_to.sh', router], stdout=PIPE, stdin=PIPE, stderr=STDOUT)
		p.communicate(input=vtysh_cmd)[0]
		print(p.returncode)

	print("########################################################################################################")