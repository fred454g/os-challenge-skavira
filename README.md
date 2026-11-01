# os-challenge-skavira

TCP server for the OS Challenge in course 02159: the client sends a SHA-256 hash and a search interval, and the server finds and returns the correct number.

## Structure

- `server.c`: `main()` reads the port, creates the listening socket and, for one connection at a time, runs `accept → read_request → find_answer → send_answer → close`.
- `include/server_helpers.h`: `struct request` (the decoded 49-byte request) and declarations of the helper functions.
- `src/server_helpers.c`: The helper functions `create_listening_socket`, `read_request`, `find_answer` (brute force with lonesha256) and `send_answer`.
- `include/messages.h`: Packet sizes and offsets for byte mapping.
- `include/lonesha256.h`: SHA-256 function; the implementation is included in `src/server_helpers.c`.
- `Makefile`: Builds `server` from `server.c` and `src/server_helpers.c`.

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

Requires a C compiler and `make`.
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

![alt text](image.png)

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

# Windows (Vagrant + VirtualBox)

On Windows, don't build natively (PowerShell/MSYS2/MinGW). The server uses POSIX sockets, which MinGW doesn't have, and it gets graded on Linux. Use the two Vagrant VMs instead. They copy the grading setup.

## The mental model

There are three layers. Each command runs in exactly one of them:

```
 WINDOWS HOST  (PowerShell, in C:\DTU-local\Repos\os-challenge-skavira\x86_64)
 │   You edit code here (VS Code etc.) and run the vagrant commands here.
 │   vagrant up / halt / destroy / ssh  ── controls VirtualBox
 │
 ├── VM "server"  192.168.101.10   (Ubuntu 24.04, 4 CPUs, 512 MB)
 │     /home/vagrant/os-challenge-common/   ← THIS REPO, live-shared from Windows
 │     make clean && make
 │     ./server 5003                        ← your server listens here
 │
 └── VM "client"  192.168.101.11   (Ubuntu 24.04, 2 CPUs, 512 MB)
       /home/vagrant/os-challenge-common/   ← the same shared folder
       ./run-client.sh                      ← sends requests to 192.168.101.10:5003
```

Key points:

- **The repo root is the shared folder.** The `Vagrantfile` in `x86_64/` mounts `../` (the repo root) at `/home/vagrant/os-challenge-common/` in both VMs. The folder name says "common", but it contains *this* repo. You don't need to clone or copy anything into the VMs: save a file in Windows and the VMs see the change immediately.
- **Vagrant ties VMs to the folder you run it in.** It records which VMs belong to a Vagrantfile in the `.vagrant/` folder next to it. So always run `vagrant` commands from `os-challenge-skavira\x86_64`. Running them from another folder (e.g. a separate `os-challenge-common` clone) talks to a different set of VMs, or says "VM not created".
- **The two VMs talk over a private network.** That network exists only between the VMs and your PC. The client scripts have `192.168.101.10:5003` hardcoded, so the server must run on port **5003** in the server VM.
- **Compile inside the VM, not on Windows.** The `server` binary is a Linux program.

## One-time setup

1. Install **VirtualBox** and **Vagrant ≥ 2.4** (older Vagrant versions can't read the bento box metadata and fail with "doesn't support the provider").
2. If Hyper-V / WSL2 / "Virtual Machine Platform" is enabled, VirtualBox may be very slow or refuse to start VMs. Hardware virtualization (VT-x/AMD-V) must be enabled in the BIOS.
3. Download the box and create the VMs (the first `vagrant up` takes several minutes because it runs `bootstrap.sh`: apt upgrade, git, build-essential):

```powershell
cd C:\DTU-local\Repos\os-challenge-skavira\x86_64
vagrant box add bento/ubuntu-24.04 --provider virtualbox --architecture amd64
vagrant up
```

`x86_64/Vagrantfile` sets `box_architecture = "amd64"` for both VMs. Without it, Vagrant on Windows fails to match the downloaded box and `vagrant up` tries (and fails) to download it again.

## Everyday workflow

Open **two PowerShell windows**, both in `C:\DTU-local\Repos\os-challenge-skavira\x86_64`.

```powershell
vagrant up              # start both VMs (fast after the first time; no-op if already running)
```

**Window 1 – server VM (start this first):**

```powershell
vagrant ssh server
```
```sh
cd ~/os-challenge-common
make clean && make
./server 5003
```

**Window 2 – client VM:**

```powershell
vagrant ssh client
```
```sh
cd ~/os-challenge-common
chmod +x "$(./get-bin-path.sh)/client"
./run-client.sh                 # small sanity test (20 requests)
```

**Edit–test loop:** edit code in Windows → in window 1 press `Ctrl+C`, run `make clean && make && ./server 5003` → in window 2 run `./run-client.sh` again.

**Commit and push from Windows** as usual. The VMs only build and run the code.

## Test scripts (run in the client VM)

| Script | What it does |
| --- | --- |
| `./run-client.sh` | Small quick test: 20 requests, low difficulty. Use for "does it work at all". |
| `./run-client-milestone.sh` | Exact milestone configuration: 100 requests, difficulty 30M, 20 % repeats, priorities. |
| `./run-client-final.sh` | Final-run configuration: 500 requests (the real seed is secret). |

**Reference server:** to check that the VM setup works, or to get a baseline score, run the course's reference server in the server VM instead of ours, then run a client script as normal:

```sh
cd ~/os-challenge-common
chmod +x "$(./get-bin-path.sh)/server"
./run-reference-server.sh       # listens on 5003, same as ours
```

The client prints its results when it finishes.

## Vagrant command cheat sheet (PowerShell, in `x86_64\`)

| Command | Effect |
| --- | --- |
| `vagrant status` | Shows the state of `server` and `client` (running / poweroff / not created). |
| `vagrant up` | Creates the VMs if missing (and provisions them), otherwise boots them. |
| `vagrant ssh server` / `vagrant ssh client` | Opens a shell in that VM. `exit` leaves it; the VM keeps running. |
| `vagrant halt` | Shuts both VMs down cleanly. Everything is kept. |
| `vagrant reload` | Restarts the VMs and applies `Vagrantfile` changes. |
| `vagrant provision` | Re-runs `bootstrap.sh` on running VMs. |
| `vagrant destroy -f` | Deletes the VMs completely. The next `vagrant up` builds them from scratch. Your code is safe (it lives on Windows). |

Add a machine name to target only one, e.g. `vagrant halt client`.

Shutting down: stop the client, then `Ctrl+C` the server, `exit` both shells, and `vagrant halt`.

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| `VM must be created before running this command` / `VM not created`, but VirtualBox shows `server` and `client` running | Those VMs were created from **another folder** (e.g. a separate `os-challenge-common\x86_64`), so this folder's `.vagrant\` doesn't know them. Go to that folder and run `vagrant destroy -f` (or delete the VMs in VirtualBox Manager), then run `vagrant up` here. |
| `Could not rename the directory ... VirtualBox VMs\server ... (VERR_ALREADY_EXISTS)` | A VM or folder named `server`/`client` already exists (the Vagrantfile renames the VMs to those names). Run `vagrant destroy -f` here, remove the old VM in VirtualBox Manager (or delete the orphan folder in `C:\Users\<you>\VirtualBox VMs\`), then `vagrant up`. |
| `The box you're attempting to add doesn't support the provider` | Vagrant older than 2.4, or a missing `--architecture amd64` / `box_architecture`. See setup. |
| `/bin/sh^M: bad interpreter` or `$'\r': command not found` when running a script in a VM | Git on Windows checked the scripts out with CRLF line endings. Run `git config core.autocrlf input` in the repo and re-checkout, or run `sed -i 's/\r$//' *.sh` inside the VM. |
| Client: `Connection refused` | The server isn't running in the server VM, or it's on a port other than 5003. |
| `Permission denied` running the client/reference server | Run the `chmod +x "$(./get-bin-path.sh)/client"` (or `/server`) line. |
| `vagrant up` hangs at "Waiting for machine to boot" | Usually a Hyper-V conflict or virtualization disabled in the BIOS. |
