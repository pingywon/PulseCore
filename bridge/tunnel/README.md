# Putting a PulseFeed controller on the internet

Three ways, in the order you should prefer them.

Whichever you choose, the rule is the same: **the tunnel or proxy points at
the bridge, never at the device.** The bridge is the thing with the mode
ladder, the rate limiter and the audit log. Pointing a tunnel straight at
the controller throws all of that away.

```
  internet  ->  cloudflared / nginx / caddy  ->  pulsefeed-bridge  ->  device
                (TLS, public name)              (authz, policy, audit)  (:80)
```

---

## 1. Cloudflare Tunnel  (recommended)

No port forwarding, no public IP, no inbound firewall hole. The tunnel
daemon makes an outbound connection and Cloudflare terminates TLS.

```bash
# once
cloudflared tunnel login
cloudflared tunnel create pulsefeed
cloudflared tunnel route dns pulsefeed pulsefeed.example.com

# run the bridge first
./pulsefeed-bridge.py --target http://192.168.13.50 \
    --listen 127.0.0.1:8443 --mode readonly \
    --trusted-proxy 127.0.0.1 --audit /var/log/pulsefeed-audit.jsonl

# then the tunnel
cloudflared tunnel --config tunnel/cloudflared.yml run pulsefeed
```

Bind the bridge to `127.0.0.1` when a tunnel is in front of it. There is no
reason for it to be reachable on the LAN as well, and binding it wide is how
people accidentally expose the unprotected side.

Add Cloudflare Access on top if you want SSO instead of the share key.

## 2. nginx / Caddy on a VPS

Use when you already run a server. `nginx.conf` and `Caddyfile` in this
directory are complete, working examples. Caddy gets you automatic
Let's Encrypt certificates with no extra work.

You need a WireGuard or Tailscale link from the VPS back to the bridge, or
the bridge running on the VPS itself with a VPN to the device network.

## 3. Direct port forward

Don't. If you must: TLS on the bridge (`--cert`/`--key`), `--mode readonly`,
`--allow-cidr` locked to known addresses, and a share key you rotate.

---

## Mode ladder, in one table

| mode        | read | stop / e-stop | turn output down | change patterns | start |
|-------------|:----:|:-------------:|:----------------:|:---------------:|:-----:|
| `readonly`  |  y   |       y       |         -        |        -        |   -   |
| `supervise` |  y   |       y       |         y        |        -        |   -   |
| `control`   |  y   |       y       |         y        |        y        |   -   |
| `full`      |  y   |       y       |         y        |        y        |   y   |

`full` requires `--i-understand` on the command line. It lets someone who is
not in the room start a machine.
