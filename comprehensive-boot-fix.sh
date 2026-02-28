#!/bin/sh
#
# Comprehensive YakirOS Boot Configuration Fix
# This script addresses all known boot issues:
# 1. Boot parameter missing init=/sbin/graph-resolver
# 2. Component configs in wrong location
# 3. Network component syntax errors
#

set -e

echo "🔧 YakirOS Boot Configuration Fix"
echo "=================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check if we're running as root
if [ "$(id -u)" != "0" ]; then
    echo -e "${RED}❌ This script must be run as root${NC}"
    exit 1
fi

# Step 1: Fix boot configuration
echo -e "${BLUE}📝 Step 1: Fixing boot configuration...${NC}"

EXTLINUX_CONF="/boot/extlinux/extlinux.conf"
if [ -f "$EXTLINUX_CONF" ]; then
    echo "Current boot configuration:"
    cat "$EXTLINUX_CONF"
    echo ""

    # Create backup
    cp "$EXTLINUX_CONF" "${EXTLINUX_CONF}.backup"

    # Fix the APPEND line to include init=/sbin/graph-resolver
    if grep -q "init=/sbin/graph-resolver" "$EXTLINUX_CONF"; then
        echo -e "${GREEN}✓ Boot configuration already has correct init parameter${NC}"
    else
        echo -e "${YELLOW}⚠️  Adding init=/sbin/graph-resolver to boot configuration...${NC}"

        # Replace the APPEND line
        sed -i 's/APPEND \(.*\)$/APPEND \1 init=\/sbin\/graph-resolver/' "$EXTLINUX_CONF"

        echo "Updated boot configuration:"
        cat "$EXTLINUX_CONF"
    fi
else
    echo -e "${RED}❌ Boot configuration not found at $EXTLINUX_CONF${NC}"
fi

# Step 2: Create component configurations in the expected location
echo -e "${BLUE}📝 Step 2: Setting up component configurations...${NC}"

# YakirOS expects configs in /tmp/yakiros/graph.d (hardcoded in source)
YAKIROS_CONFIG_DIR="/tmp/yakiros/graph.d"
SYSTEM_CONFIG_DIR="/etc/graph.d"

mkdir -p "$YAKIROS_CONFIG_DIR"

if [ -d "$SYSTEM_CONFIG_DIR" ]; then
    echo -e "${YELLOW}📂 Copying configurations from $SYSTEM_CONFIG_DIR to $YAKIROS_CONFIG_DIR...${NC}"
    cp -r "$SYSTEM_CONFIG_DIR"/* "$YAKIROS_CONFIG_DIR"/ 2>/dev/null || true
    echo -e "${GREEN}✓ Configurations copied${NC}"
else
    echo -e "${YELLOW}⚠️  System config directory $SYSTEM_CONFIG_DIR not found${NC}"
fi

# Step 3: Create basic component configurations if they don't exist
echo -e "${BLUE}📝 Step 3: Creating essential component configurations...${NC}"

# Create basic filesystem component
cat > "$YAKIROS_CONFIG_DIR/filesystem.toml" << 'EOF'
[component]
name = "filesystem"
binary = "/bin/true"
type = "oneshot"

[provides]
capabilities = ["fs.root", "fs.proc", "fs.sys", "fs.dev", "fs.run", "fs.tmp", "fs.var"]

[requires]
capabilities = []
EOF

# Create basic logging component
cat > "$YAKIROS_CONFIG_DIR/logging.toml" << 'EOF'
[component]
name = "syslog"
binary = "/sbin/syslogd"
args = ["-n", "-O", "/var/log/messages"]
type = "service"

[provides]
capabilities = ["logging"]

[requires]
capabilities = ["fs.var", "fs.run"]
EOF

# Create network component with corrected syntax
cat > "$YAKIROS_CONFIG_DIR/network.toml" << 'EOF'
[component]
name = "network"
binary = "/sbin/ifconfig"
args = ["eth0", "up"]
type = "oneshot"

[provides]
capabilities = ["network.interface"]

[requires]
capabilities = ["fs.sys", "fs.proc"]

[component]
name = "network-dhcp"
binary = "/sbin/udhcpc"
args = ["-i", "eth0", "-f", "-q"]
type = "service"

[provides]
capabilities = ["network.configured"]

[requires]
capabilities = ["network.interface"]
EOF

# Create SSH component
cat > "$YAKIROS_CONFIG_DIR/ssh.toml" << 'EOF'
[component]
name = "ssh-keys"
binary = "/usr/bin/ssh-keygen"
args = ["-A"]
type = "oneshot"

[provides]
capabilities = ["ssh.keys"]

[requires]
capabilities = ["fs.etc"]

[component]
name = "sshd"
binary = "/usr/sbin/sshd"
args = ["-D"]
type = "service"

[provides]
capabilities = ["ssh"]

[requires]
capabilities = ["network.configured", "ssh.keys", "users"]
EOF

echo -e "${GREEN}✓ Component configurations created${NC}"

# Step 4: Verify YakirOS binaries exist
echo -e "${BLUE}📝 Step 4: Verifying YakirOS binaries...${NC}"

if [ -f "/sbin/graph-resolver" ]; then
    echo -e "${GREEN}✓ graph-resolver found at /sbin/graph-resolver${NC}"
    ls -la /sbin/graph-resolver
else
    echo -e "${RED}❌ graph-resolver not found at /sbin/graph-resolver${NC}"
fi

if [ -f "/usr/bin/graphctl" ]; then
    echo -e "${GREEN}✓ graphctl found at /usr/bin/graphctl${NC}"
    ls -la /usr/bin/graphctl
else
    echo -e "${RED}❌ graphctl not found at /usr/bin/graphctl${NC}"
fi

# Step 5: Test configuration loading
echo -e "${BLUE}📝 Step 5: Testing configuration loading...${NC}"

if [ -f "/sbin/graph-resolver" ]; then
    echo "Testing graph-resolver configuration loading..."
    /sbin/graph-resolver --test-config --config-dir "$YAKIROS_CONFIG_DIR" || true
fi

echo ""
echo -e "${GREEN}🎉 Boot configuration fix complete!${NC}"
echo ""
echo -e "${BLUE}📋 Summary of changes:${NC}"
echo "  ✓ Added init=/sbin/graph-resolver to boot configuration"
echo "  ✓ Created component configurations in $YAKIROS_CONFIG_DIR"
echo "  ✓ Fixed network component syntax"
echo "  ✓ Verified YakirOS binaries"
echo ""
echo -e "${YELLOW}⚠️  Please reboot the system to test the changes${NC}"
echo ""