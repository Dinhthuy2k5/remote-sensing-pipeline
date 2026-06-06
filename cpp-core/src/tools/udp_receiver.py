import socket
import json
import datetime

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", 9090))
    sock.settimeout(10.0)

    print("=" * 60)
    print("  UDP Metrics Receiver — port 9090")
    print("=" * 60)

    try:
        while True:
            try:
                data, addr = sock.recvfrom(4096)
                m = json.loads(data.decode())

                ts = datetime.datetime.now().strftime("%H:%M:%S")

                # Progress bar
                total = m.get("tiles_total", 0)
                done  = m.get("tiles_done",  0)
                pct   = int(done * 100 / total) if total > 0 else 0
                bar   = "█" * (pct // 5) + "░" * (20 - pct // 5)

                print(
                    f"[{ts}] "
                    f"session={m.get('session_id',-1):>3} "
                    f"state={m.get('state','?'):<12} "
                    f"cpu={m.get('cpu_percent',0):>5.1f}% "
                    f"ram={m.get('ram_used_mb',0):>5}MB "
                    f"fps={m.get('fps',0):>5.1f} "
                    f"[{bar}] {pct:>3}% "
                    f"({done}/{total})"
                )

            except socket.timeout:
                print(f"[{datetime.datetime.now().strftime('%H:%M:%S')}] "
                      f"Waiting for broadcast...")

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()

if __name__ == "__main__":
    main()