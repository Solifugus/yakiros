#!/bin/bash
#
# YakirOS Boot Configuration Fix
# =============================
#
# This script fixes the boot configuration issues that prevent YakirOS from running properly.
# It addresses the core problem: YakirOS expects configs in /tmp/yakiros/graph.d but they're
# installed to /etc/graph.d by the setup script.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() { echo -e "${BLUE}ℹ️  $1${NC}"; }
log_success() { echo -e "${GREEN}✅ $1${NC}"; }
log_warn() { echo -e "${YELLOW}⚠️  $1${NC}"; }
log_error() { echo -e "${RED}❌ $1${NC}"; }

echo -e "${BLUE}🔧 YakirOS Boot Configuration Fix${NC}"
echo "=================================="

# Check if we have access to the VM filesystem
if [ "$1" = "--create-fixed-vm" ]; then
    log_info "Creating a properly configured YakirOS VM from scratch..."

    # Create a modified version of the setup script that fixes the config path issue
    if [ ! -f "setup-yakiros-vm.sh" ]; then
        log_error "setup-yakiros-vm.sh not found in current directory"
        exit 1
    fi

    # Create a fixed version of the setup script
    log_info "Creating fixed setup script..."

    # Copy the original setup script
    cp setup-yakiros-vm.sh setup-yakiros-vm-fixed.sh

    # Modify the installation section to fix the config directory issue
    # Find the line that installs configs and add a fix after it
    sed -i '/install -m 644 config\/\*.toml \/etc\/graph\.d\//a\\n# Fix: YakirOS expects configs in /tmp/yakiros/graph.d (hardcoded in source)\ninstall -d /tmp/yakiros/graph.d\ncp /etc/graph.d/* /tmp/yakiros/graph.d/ 2>/dev/null || true\necho "Fixed config directory for YakirOS hardcoded path"' setup-yakiros-vm-fixed.sh

    # Also add a fix to ensure the config survives reboots
    sed -i '/echo "🚀 System will boot with YakirOS as PID 1 on next reboot"/i\\n# Ensure YakirOS configs persist across reboots\necho "#!/bin/sh" > /etc/local.d/yakiros-config.start\necho "mkdir -p /tmp/yakiros/graph.d" >> /etc/local.d/yakiros-config.start\necho "cp /etc/graph.d/* /tmp/yakiros/graph.d/ 2>/dev/null || true" >> /etc/local.d/yakiros-config.start\nchmod +x /etc/local.d/yakiros-config.start\nrc-update add local\necho "Added startup script to fix YakirOS config path"\n' setup-yakiros-vm-fixed.sh

    log_success "Fixed setup script created: setup-yakiros-vm-fixed.sh"
    log_info "Run './setup-yakiros-vm-fixed.sh' to create a properly configured VM"

    # Show what was fixed
    echo ""
    log_info "Fixes applied:"
    echo "  1. Added config directory fix during installation"
    echo "  2. Added startup script to ensure configs persist across reboots"
    echo "  3. Configs will be available in both /etc/graph.d and /tmp/yakiros/graph.d"
    echo ""

    exit 0
fi

# If running inside VM, apply fixes directly
if [ "$(id -u)" != "0" ]; then
    log_error "This script must be run as root inside the VM"
    exit 1
fi

log_info "Applying YakirOS boot configuration fixes..."

# Step 1: Verify YakirOS binaries exist
log_info "Step 1: Verifying YakirOS installation..."
if [ -f "/sbin/graph-resolver" ]; then
    log_success "graph-resolver found at /sbin/graph-resolver"
else
    log_error "graph-resolver not found at /sbin/graph-resolver"
    exit 1
fi

# Step 2: Fix config directory issue
log_info "Step 2: Fixing configuration directory..."
YAKIROS_CONFIG_DIR="/tmp/yakiros/graph.d"
SYSTEM_CONFIG_DIR="/etc/graph.d"

# Create YakirOS expected directory
mkdir -p "$YAKIROS_CONFIG_DIR"

# Copy configs from system location to YakirOS expected location
if [ -d "$SYSTEM_CONFIG_DIR" ] && [ "$(ls -A $SYSTEM_CONFIG_DIR 2>/dev/null)" ]; then
    log_info "Copying configurations from $SYSTEM_CONFIG_DIR to $YAKIROS_CONFIG_DIR"
    cp "$SYSTEM_CONFIG_DIR"/* "$YAKIROS_CONFIG_DIR"/
    log_success "Configurations copied"
else
    log_warn "No configurations found in $SYSTEM_CONFIG_DIR"
fi

# Step 3: Verify boot configuration
log_info "Step 3: Checking boot configuration..."
EXTLINUX_CONF="/boot/extlinux/extlinux.conf"
if [ -f "$EXTLINUX_CONF" ]; then
    if grep -q "init=/sbin/graph-resolver" "$EXTLINUX_CONF"; then
        log_success "Boot configuration already has correct init parameter"
    else
        log_warn "Boot configuration missing init parameter"
        log_info "Current boot config:"
        grep "APPEND" "$EXTLINUX_CONF" || echo "No APPEND line found"
    fi
else
    log_error "Boot configuration not found at $EXTLINUX_CONF"
fi

# Step 4: Create startup script to ensure configs persist
log_info "Step 4: Creating startup script for config persistence..."
cat > /etc/local.d/yakiros-config.start << 'EOF'
#!/bin/sh
# Ensure YakirOS configuration directory exists and is populated
mkdir -p /tmp/yakiros/graph.d
if [ -d /etc/graph.d ] && [ "$(ls -A /etc/graph.d 2>/dev/null)" ]; then
    cp /etc/graph.d/* /tmp/yakiros/graph.d/ 2>/dev/null || true
fi
EOF

chmod +x /etc/local.d/yakiros-config.start
rc-update add local 2>/dev/null || true

log_success "Startup script created"

# Step 5: List available configurations
log_info "Step 5: Available configurations:"
if [ -d "$YAKIROS_CONFIG_DIR" ]; then
    ls -la "$YAKIROS_CONFIG_DIR"
else
    log_warn "No configurations found in $YAKIROS_CONFIG_DIR"
fi

echo ""
log_success "YakirOS boot configuration fix complete!"
log_info "The system should now boot properly with YakirOS as PID 1"
log_warn "Please reboot to test the changes: reboot"
echo ""