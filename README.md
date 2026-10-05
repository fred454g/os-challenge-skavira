# os-challenge-skavira

TCP server for the OS Challenge in course 02159: the client sends a SHA-256 hash and a search interval, and the server finds and returns the correct number.

## Structure

- `server.c`: `main()` reads the port, creates a socket, and runs `accept → recv → close` for one client at a time.
- `include/server_helpers.h`: Declarations for helper functions.
- `src/server_helpers.c`: Implementation of helper functions, e.g., for decoding requests and finding the answer. Both helper files are currently just skeletons.
- `include/messages.h`: Packet sizes and offsets for byte mapping.
- `include/lonesha256.h`: SHA-256 function; the implementation is included in `server.c`.
- `Makefile`: Builds `server`. NOTE: Currently, it only compiles `server.c`; add `src/server_helpers.c` to both dependencies and the compiler command when helpers are needed

## Packet Mapping

A request is **49 bytes**. Use `PACKET_REQUEST_*` from `messages.h`:

| Bytes | Content | Offset Constant |
| --- | --- | --- |
| 0–31 | SHA-256 hash | `PACKET_REQUEST_HASH_OFFSET` |
| 32–39 | Search interval start (64-bit) | `PACKET_REQUEST_START_OFFSET` |
| 40–47 | Search interval end (64-bit) | `PACKET_REQUEST_END_OFFSET` |
| 48 | Priority | `PACKET_REQUEST_PRIO_OFFSET` |

The client sends the interval boundaries in big-endian. The expected response is **8 bytes** containing the found number (`PACKET_RESPONSE_SIZE`, offset `PACKET_RESPONSE_ANSWER_OFFSET`).


## Run Locally

Requires a C compiler, `make`, and OpenSSL. On macOS: `brew install openssl@3`.
On Ubuntu/Debian: `sudo apt install build-essential libssl-dev`.
Run the commands from the project root.

**Terminal 1 – Build and start the server:**

```sh
make clean && make
./server 8080
```

**Terminal 2 – Send a simple request:**
```sh
client_bin=arm64/bin/macos/client
# Linux ARM64: client_bin=arm64/bin/linux/client
# Linux x86_64: client_bin=x86_64/bin/linux/client
chmod +x "$client_bin"
"./$client_bin" 127.0.0.1 8080 0 1 1 1 0 0 0
```
The client arguments are `host port seed total start difficulty rep delay lambda`.
The server should print `Recieved 49 bytes:` followed by the packet contents. The client might hang because the response logic is missing; terminate with `Ctrl+C`.
After code changes: stop the server with `Ctrl+C`, run `make clean && make`, and start it again.
No native macOS client for Intel is provided; use the VM flow there instead.

# Mac

Requires Vagrant and the VMware Desktop provider for `arm64/` or VirtualBox for `x86_64/`.
Run `vagrant up` from the relevant directory and open two terminals using `vagrant ssh server` and `vagrant ssh client` respectively from the same directory.

The project root is shared as `/home/vagrant/os-challenge-common/` in both VMs.
Install `libssl-dev` in the server VM if it is not already installed: `sudo apt install libssl-dev`.

If both VM terminals are already open, use the following commands.

**Server terminal (`vagrant ssh server`) – start this first:**

```sh
cd /home/vagrant/os-challenge-common
make clean && make
./server 5003
```

**Client terminal (`vagrant ssh client`):**

```sh
cd /home/vagrant/os-challenge-common
chmod +x "$(./get-bin-path.sh)/client"
./run-client.sh
```

Keep the server running while the client sends requests. Run `./run-client.sh` again to repeat the test.
The script uses server IP `192.168.101.10` and port `5003`; the client VM has IP `192.168.101.11`.

After code changes on the host, stop the server with `Ctrl+C`, run `make clean && make` in the server VM, and start it again with `./server 5003`.

## Stopping the client, server, and VMs

1. Let the client finish its requests and return to the prompt. To interrupt a running or stuck client, press `Ctrl+C` in the client terminal.
2. Once the client has finished, press `Ctrl+C` in the server terminal. The server does not currently have a graceful shutdown handler, so let requests finish before stopping it.
3. Run `exit` in both terminals to leave the SSH sessions. This disconnects the terminals but leaves the VMs running.
4. To shut down both VMs cleanly, run the following on your Mac from the same `arm64/` or `x86_64/` directory where you ran `vagrant up`:

   ```sh
   vagrant halt
   ```

Use `vagrant up` from that directory to start the VMs again, then reconnect with `vagrant ssh server` and `vagrant ssh client` and run the startup commands above.

# Windows local dev
