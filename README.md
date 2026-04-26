# Hermes

Encrypted file transfer tool. Send files from any machine to your home computer over a network. Files are AES-256 encrypted in transit — password and content are never visible on the wire.

---

## How it works

```
hermes notes.txt
      │
      ▼
aes_encrypt(filename + password + content)
      │
      ▼
TCP socket → your Windows IP → portproxy → WSL server
      │
      ▼
aes_decrypt → check password → save to Sent_Files/
```

---

## Requirements

- Linux / WSL2 (Ubuntu)
- OpenSSL
- g++ with C++17
- inotify-tools (for folder watcher)

```bash
sudo apt install libssl-dev inotify-tools
```

---

## Server Setup (On Linux or WSL on home machine)

### 1. Clone and compile

```bash
git clone https://github.com/KingVelzard/Hermes.git
cd Hermes
g++ -std=c++17 Reactor.cpp main.cpp -o server -lssl -lcrypto -lpthread
```

### 2. Generate AES key and IV

```bash
echo "HERMES_AES_KEY=$(openssl rand -base64 24 | tr -d '=+/' | cut -c1-32)"
echo "HERMES_AES_IV=$(openssl rand -base64 12 | tr -d '=+/' | cut -c1-16)"
```

Copy the output — you'll need these on both machines.

### 3. Create the env file on server

```bash
nano ~/.config/systemd/user/hermes.env
```

```
HERMES_PASSWORD=yourpassword
HERMES_AES_KEY=your32charkey
HERMES_AES_IV=your16chariv
```

```bash
chmod 600 ~/.config/systemd/user/hermes.env
```

### 4. Create Sent_Files folder

```bash
mkdir -p whereEverYouWant/Sent_Files
```

### 5. Create systemd service -- if you want as a linux service at all times

```bash
nano ~/.config/systemd/user/hermes.service
```

```ini
[Unit]
Description=Hermes file server
After=network.target

[Service]
EnvironmentFile=/home/<you>/.config/systemd/user/hermes.env
ExecStartPre=/usr/bin/touch Path/Sent_Files/.startup
ExecStart=Path/server
WorkingDirectory=Path/Sent_Files
Restart=always
RestartSec=3

[Install]
WantedBy=default.target
```

### 6. Create folder watcher service -- touches the sent_files so that you can see after each transfer

```bash
nano ~/.local/bin/hermes-watch
```

```bash
#!/bin/bash
FOLDER="Path/Sent_Files"
while true; do
    inotifywait -e create "$FOLDER" 2>/dev/null
    touch "$FOLDER"
done
```

```bash
chmod +x ~/.local/bin/hermes-watch
nano ~/.config/systemd/user/hermes-watch.service
```

```ini
[Unit]
Description=Hermes folder watcher
After=hermes.service

[Service]
ExecStart=/home/<you>/.local/bin/hermes-watch
Restart=always
RestartSec=3

[Install]
WantedBy=default.target
```

### 7. Enable everything

```bash
systemctl --user daemon-reload
systemctl --user enable hermes hermes-watch
systemctl --user start hermes hermes-watch
systemctl --user status hermes
```

YAY!!!
You can port forward to WSL if you use that. And this also works to transfer to OneDrive (weird but if you'd like)

## Client Setup (sending machine)

### 1. Compile the client

```bash
git clone https://github.com/KingVelzard/Hermes.git
cd Hermes
g++ -std=c++17 client.cpp -o client -lssl -lcrypto
mkdir -p ~/.local/bin
mv client ~/.local/bin/client
```

### 2. Add to PATH

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### 3. Create env file with your key and IV

```bash
nano ~/.hermes.env
```

```
HERMES_AES_KEY=your32charkey
HERMES_AES_IV=your16chariv
```

```bash
chmod 600 ~/.hermes.env
```

Must match the server's `hermes.env`.

### 4. Create the hermes command

```bash
> ~/.local/bin/hermes
echo '#!/bin/bash' >> ~/.local/bin/hermes
echo 'source ~/.hermes.env' >> ~/.local/bin/hermes
echo 'SERVER_IP="your.IP"' >> ~/.local/bin/hermes
echo 'CLIENT="$HOME/.local/bin/client"' >> ~/.local/bin/hermes
echo 'if [ $# -eq 0 ]; then' >> ~/.local/bin/hermes
echo '    echo "Usage: hermes <file1> [file2 ...]"' >> ~/.local/bin/hermes
echo '    exit 1' >> ~/.local/bin/hermes
echo 'fi' >> ~/.local/bin/hermes
echo 'read -sp "Password: " PASSWORD' >> ~/.local/bin/hermes
echo 'echo' >> ~/.local/bin/hermes
echo '$CLIENT $SERVER_IP "$PASSWORD" "$HERMES_AES_KEY" "$HERMES_AES_IV" "$@"' >> ~/.local/bin/hermes
chmod +x ~/.local/bin/hermes
```

---

## Usage

```bash
hermes notes.txt
hermes homework.cpp notes.pdf image.png
```

You'll be prompted for your password once per invocation. Files arrive in `Sent_Files` with a timestamp prefix:

```
notes.txt_received_1745123456
homework.cpp_received_1745123457
```

---

## Troubleshooting

**Server not saving files:**
```bash
systemctl --user status hermes
journalctl --user -u hermes -f
```

**Connection timing out from another machine:**
- Check portproxy is set: `netsh interface portproxy show all`
- Check firewall rule exists
- IF USING WSL: WSL IP may have changed after reboot — redo the portproxy command

**File shows up but Explorer doesn't refresh:**
- Press F5 in File Explorer
- Check `hermes-watch` service is running: `systemctl --user status hermes-watch`
