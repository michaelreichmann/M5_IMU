1) Setup overview (what the rig is)
Router (no internet):
2.4 GHz SSID: IMU_In (WPA2-PSK, password sonification).
5 GHz SSID: any name, same LAN, AP/Client isolation OFF.
Raspberry Pi 5 on Ethernet with static IP 192.168.1.50.
M5StickC Plus2 (ESP32): connect to IMU_In, send OSC/UDP to 192.168.1.50:9000.
Pi fan-out: broadcasts every packet to 192.168.1.255:8000. Laptops just join the 5 GHz SSID and receive.
No internet required.


2) Configure the Pi (once)
2.1 Give the Pi a static IP on Ethernet
# Disable NetworkManager (we're using systemd-networkd)
sudo systemctl disable --now NetworkManager

# Static IPv4 for eth0
sudo mkdir -p /etc/systemd/network
sudo nano /etc/systemd/network/10-eth0.network
Paste:
[Match]
Name=eth0

[Network]
Address=192.168.1.50/24
Gateway=192.168.1.1
DNS=192.168.1.1
Enable + start:
sudo systemctl enable --now systemd-networkd
Plug in the Ethernet cable to the router. Verify:
ip addr show eth0        # shows 192.168.1.50/24 and state UP
ping -c3 192.168.1.1     # router replies
(We also created a simple resolver file; internet isn’t needed, but this is what we used:)
echo "nameserver 192.168.1.1" | sudo tee /etc/resolv.conf
2.2 Install the fan-out script and service
Create the script:
sudo mkdir -p /opt/imu-fanout
sudo nano /opt/imu-fanout/fanout.py
Paste this (includes the QoS line we added):
#!/usr/bin/env python3
import argparse, socket, selectors, time, sys

def make_in_socket(listen_ip, in_port, rcvbuf=4*1024*1024):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try: s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
    except OSError: pass
    s.bind((listen_ip, in_port))
    return s

def make_out_socket_broadcast(bcast_ip, out_port, sndbuf=4*1024*1024):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_TOS, 0xB8)  # DSCP EF (voice)
    try: s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, sndbuf)
    except OSError: pass
    return s, (bcast_ip, out_port)

def main():
    ap = argparse.ArgumentParser(description="IMU UDP fan-out")
    ap.add_argument("--listen", default="0.0.0.0")
    ap.add_argument("--in-port", type=int, default=9000)
    ap.add_argument("--out-port", type=int, default=8000)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--broadcast", metavar="BCAST_IP")   # e.g. 192.168.1.255
    g.add_argument("--multicast", metavar="MCAST_IP")   # unused in our setup
    ap.add_argument("--stats", action="store_true")
    args = ap.parse_args()

    in_sock = make_in_socket(args.listen, args.in_port)
    out_sock, out_addr = make_out_socket_broadcast(args.broadcast, args.out_port)

    sel = selectors.DefaultSelector()
    sel.register(in_sock, selectors.EVENT_READ)

    count = 0; t0 = time.time()
    print(f"Listening on {args.listen}:{args.in_port}")
    print(f"Forwarding to {out_addr[0]}:{out_addr[1]} (broadcast)")
    while True:
        events = sel.select(timeout=1.0)
        for key, _ in events:
            if key.fileobj is in_sock:
                try:
                    data, _src = in_sock.recvfrom(2048)
                    out_sock.sendto(data, out_addr)
                    count += 1
                except Exception as e:
                    print(f"warn: {e}", file=sys.stderr)
        if args.stats and time.time() - t0 >= 1.0:
            print(f"{count} pkts/s")
            count = 0; t0 = time.time()

if __name__ == "__main__":
    main()
Permissions:
sudo chown -R pi:pi /opt/imu-fanout
sudo chmod +x /opt/imu-fanout/fanout.py
Service:
sudo nano /etc/systemd/system/imu-fanout.service
Paste:
[Unit]
Description=IMU UDP fan-out (broadcast)
After=network-online.target
Wants=network-online.target

[Service]
User=pi
Group=pi
ExecStart=/usr/bin/python3 /opt/imu-fanout/fanout.py --broadcast 192.168.1.255 --in-port 9000 --out-port 8000 --stats
Restart=on-failure
RestartSec=1

[Install]
WantedBy=multi-user.target
Enable + start + watch:
sudo systemctl daemon-reload
sudo systemctl enable --now imu-fanout.service
systemctl status imu-fanout.service
journalctl -u imu-fanout -f    # shows pkts/s once data arrives

3) Flash the M5StickC Plus2 (what we changed)
We kept your code and only changed what was required:
SSID → IMU_In
Password → sonification
Destination port → 9000 (Pi’s fan-in), not 8000
Turn Wi-Fi sleep off and auto-reconnect on
These are the exact edits:
// WIFI config
const char* WIFI_SSID     = "IMU_In";         // CHANGED
const char* WIFI_PASSWORD = "sonification";   // CHANGED

WiFiUDP Udp;
const IPAddress OUT_IP(192, 168, 1, 50);
// Send to Pi's fan-in port:
const uint16_t OUT_PORT = 9000;               // CHANGED (was 8000)
const uint16_t LOCAL_PORT = 9000;

// in setup(), Wi-Fi settings:
WiFi.mode(WIFI_STA);
WiFi.setSleep(false);      // CHANGED (was true)
WiFi.setAutoReconnect(true);  // ADDED
wifiEnsureConnected();
Udp.begin(LOCAL_PORT);

// when sending:
Udp.beginPacket(OUT_IP, OUT_PORT);  // uses 9000 now
Everything else in your sketch stays as-is (OSC address /m5/<id>/imu and payload [ax ay az gx gy gz]).

4) Laptops (receiving)
Connect to the 5 GHz SSID on the same LAN (no isolation).
Max/MSP: udpreceive 8000 → parse your OSC.
Terminal sanity check (macOS/BSD netcat needs -k to keep listening):
nc -u -l -k 8000

5) Daily operation (plug-and-play)
Power the router.
Plug the Pi into the router via Ethernet and power it.
Pi boots; imu-fanout.service auto-starts.
M5 devices join IMU_In and send to 192.168.1.50:9000.
Laptops join 5 GHz SSID and receive broadcast on port 8000.
Optional quick check on the Pi:
journalctl -u imu-fanout -f
(You’ll see pkts/s when sensors are sending.)

6) Notes we actually applied
AP/Client isolation = OFF on the router (you verified it in the UI).
We kept broadcast and added DSCP EF tagging in the Pi script to reduce hiccups.
We tried both 192.168.1.255 and 255.255.255.255; no audible difference → kept 192.168.1.255.
The router is not connected to the internet (by design).
