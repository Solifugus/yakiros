#!/bin/bash
# YakirOS Boot Configuration Fix Script

set -e

echo "🔧 Fixing YakirOS boot configuration..."

# Mount the YakirOS disk
mkdir -p /mnt/disk
mount /dev/sda3 /mnt/disk
mount /dev/sda1 /mnt/disk/boot

echo "✅ Disk mounted"

echo "=== Current boot configuration ==="
cat /mnt/disk/boot/extlinux.conf

# Check if YakirOS init parameter exists
if grep -q "init=/sbin/graph-resolver" /mnt/disk/boot/extlinux.conf; then
    echo "✅ YakirOS init parameter already present"
else
    echo "❌ YakirOS init parameter missing - adding it"
    # Add YakirOS init parameter
    sed -i '/APPEND.*console=ttyS0,115200/ s/$/ init=\/sbin\/graph-resolver/' /mnt/disk/boot/extlinux.conf
fi

echo "=== Updated boot configuration ==="
cat /mnt/disk/boot/extlinux.conf

# Ensure YakirOS config directory exists
mkdir -p /mnt/disk/tmp/yakiros/graph.d

echo "=== Verifying YakirOS components ==="
ls -la /mnt/disk/tmp/yakiros/graph.d/

# Unmount
umount /mnt/disk/boot
umount /mnt/disk

echo "🎉 Boot configuration verified and fixed!"
echo "YakirOS should now run as PID 1 on next boot."
echo "Run: reboot"