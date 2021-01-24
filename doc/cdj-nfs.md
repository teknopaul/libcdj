# NFS server info

XDJs export an nfs version 2 server (over UDP)

	0teknopaul@mybox:~/bzr_workspace/libcdj$ rpcinfo 192.168.1.59
	   program version netid     address                service    owner
	    100003    2    udp       0.0.0.0.8.1            nfs        unknown
	    100005    1    udp       0.0.0.0.188.148        mountd     unknown
	    100000    2    udp       0.0.0.0.0.111          portmapper unknown

Ubuntu does not seem to support version 2

0teknopaul@sbox:~/bzr_workspace/libcdj$ rpcinfo | grep " nfs "
    100003    3    tcp       0.0.0.0.8.1            nfs        superuser
    100003    4    tcp       0.0.0.0.8.1            nfs        superuser
    100003    3    udp       0.0.0.0.8.1            nfs        superuser
    100003    3    tcp6      ::.8.1                 nfs        superuser
    100003    4    tcp6      ::.8.1                 nfs        superuser
    100003    3    udp6      ::.8.1                 nfs        superuser


cd /etc/default/nfs-kernel-server 

at /etc/default/nfs-kernel-server
# Number of servers to start up
RPCNFSDCOUNT=8

# Runtime priority of server (see nice(1))
RPCNFSDPRIORITY=0

# Options for rpc.mountd.
# If you have a port-based firewall, you might want to set up
# a fixed port here using the --port option. For more information, 
# see rpc.mountd(8) or http://wiki.debian.org/SecuringNFS
# To disable NFSv4 on the server, specify '--no-nfs-version 4' here
RPCMOUNTDOPTS="--nfs-version 2 --manage-gids"

# Do you want to start the svcgssd daemon? It is only required for Kerberos
# exports. Valid alternatives are "yes" and "no"; the default is "no".
NEED_SVCGSSD=""

# Options for rpc.svcgssd.
RPCSVCGSSDOPTS=""

systemctl restart nfs-client.target


