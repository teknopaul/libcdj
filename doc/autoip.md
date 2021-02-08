# autoip

If CDJs connect to a switch without a DHCP server on the network a standard Internet protocol [rfc3927](https://tools.ietf.org/html/rfc3927) is used to assign an IP from the range `169.254/16`.

In Linux this is implemented by `avahi-autoipd`.

In theory all you need to do is run

    sudo avahi-autoipd -w [iface]

and this will assign an ip for the local machine.


In practice this fails in Ubuntu due to a bug in `/etc/avahi/avahi-autoipd.action` if your interface name has more than 9 characters.

e.g. the interface `enx001060ce107c` fails because the action script tacks `:avahi` on the end of the interface name.

    case "$1" in
        BIND)
            ip addr flush dev "$2" label "$2:avahi"
            ip addr add "$3"/16 brd 169.254.255.255 label "$2:avahi" scope link dev "$2"
            ip route add default dev "$2" metric "$METRIC" scope link ||:
            ;;

        CONFLICT|UNBIND|STOP)
            ip route del default dev "$2" metric "$METRIC" scope link ||:
            ip addr del "$3"/16 brd 169.254.255.255 label "$2:avahi" scope link dev "$2"
            ;;

I'm not sure why it does this. Perhaps to support multihomed nics with one auto assigned address?

Removing the `:avahi` suffix seems to work  (replacing the interface label with `"$2"`)

# DHCPd

CDJs do support DHCPd if its available, so you can run a `dnsmasq` or `dhcpd` server in Linux and assign IPs yourself.