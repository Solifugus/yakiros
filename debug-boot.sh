#!/bin/bash
# Debug and fix YakirOS boot configuration more aggressively

set -e

echo "🔍 Debugging YakirOS boot configuration..."

# Mount the YakirOS disk
mkdir -p /mnt/disk
mount /dev/sda3 /mnt/disk 2>/dev/null || mount /dev/sda3 /mnt/disk
mount /dev/sda1 /mnt/disk/boot 2>/dev/null || mount /dev/sda1 /mnt/disk/boot

echo "✅ Disk mounted"

echo "=== COMPLETE boot configuration file ==="
cat /mnt/disk/boot/extlinux.conf

echo ""
echo "=== Checking for YakirOS init parameter ==="
if grep -n "init=/sbin/graph-resolver" /mnt/disk/boot/extlinux.conf; then
    echo "✅ Found YakirOS init parameter"
else
    echo "❌ YakirOS init parameter NOT found"
    echo "🔧 Looking for APPEND lines to fix..."
    grep -n "APPEND" /mnt/disk/boot/extlinux.conf

    echo "🔧 Applying aggressive fix..."
    # More aggressive approach - replace the entire APPEND line
    sed -i 's/^[[:space:]]*APPEND.*root=UUID=1417a937-b43a-4423-b024-16edf1f44cdd.*/  APPEND root=UUID=1417a937-b43a-4423-b024-16edf1f44cdd modules=sd-mod,usb-storage,ext4 console=ttyS0,115200 init=\/sbin\/graph-resolver loglevel=7/' /mnt/disk/boot/extlinux.conf

    echo "✅ Applied aggressive fix"
fi

echo ""
echo "=== FINAL boot configuration ==="
cat /mnt/disk/boot/extlinux.conf

echo ""
echo "=== Verifying YakirOS components ==="
ls -la /mnt/disk/tmp/yakiros/graph.d/ 2>/dev/null || echo "❌ YakirOS config directory missing!"

echo ""
echo "=== Verifying YakirOS binary ==="
ls -la /mnt/disk/sbin/graph-resolver 2>/dev/null || echo "❌ YakirOS binary missing!"

# Unmount
umount /mnt/disk/boot
umount /mnt/disk

echo "🎉 Boot configuration debugging complete!"
echo "If YakirOS parameter is now present, reboot should work."
echo "Run: reboot"