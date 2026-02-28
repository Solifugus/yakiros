#!/bin/bash
# YakirOS Network Configuration Fix Script

set -e

echo "🔧 Fixing YakirOS network and SSH configuration..."

# Mount the YakirOS disk
mkdir -p /mnt/disk
mount /dev/sda3 /mnt/disk
mount /dev/sda1 /mnt/disk/boot

echo "✅ Disk mounted"

# Fix network configuration (correct IP route syntax)
cat > /mnt/disk/tmp/yakiros/graph.d/10-network.toml << 'EOF'
[component]
name = "network-setup"
type = "oneshot"
binary = "/bin/sh"
args = ["-c", "ip link set eth0 up && ip addr add 10.0.2.15/24 dev eth0 && ip route add default via 10.0.2.2 dev eth0"]

[requires]
capabilities = ["kernel"]

[provides]
capabilities = ["network"]
EOF

echo "✅ Network config fixed"

# Fix SSH configuration
cat > /mnt/disk/tmp/yakiros/graph.d/20-sshd.toml << 'EOF'
[component]
name = "sshd"
type = "service"
binary = "/usr/sbin/sshd"
args = ["-D", "-e"]

[requires]
capabilities = ["network"]

[provides]
capabilities = ["ssh-server"]
EOF

echo "✅ SSH config fixed"

# Show what we fixed
echo "=== Updated Network Config ==="
cat /mnt/disk/tmp/yakiros/graph.d/10-network.toml
echo
echo "=== Updated SSH Config ==="
cat /mnt/disk/tmp/yakiros/graph.d/20-sshd.toml

# Unmount
umount /mnt/disk/boot
umount /mnt/disk

echo "🎉 YakirOS configuration fixed! Ready to reboot."
echo "Run: reboot"