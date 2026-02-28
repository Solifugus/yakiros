#!/bin/bash
#
# YakirOS Virtual Machine Setup Script
# ===================================
#
# This script creates a complete VM environment for testing YakirOS as PID 1
# Replaces systemd with YakirOS init system and demonstrates readiness protocol
#
# Usage: ./setup-yakiros-vm.sh
#
# Requirements: qemu, wget, ssh

set -e

# Configuration
VM_NAME="yakiros-vm"
VM_DISK="${VM_NAME}.qcow2"
VM_SIZE="10G"
VM_RAM="2048"
VM_CPUS="2"
SSH_PORT="2222"
ALPINE_ISO="alpine-virt-3.19.1-x86_64.iso"
ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/${ALPINE_ISO}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

log_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

log_warn() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

log_error() {
    echo -e "${RED}❌ $1${NC}"
}

log_step() {
    echo -e "${PURPLE}🔧 $1${NC}"
}

# Check prerequisites
check_prerequisites() {
    log_step "Checking prerequisites..."

    local missing=()

    if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
        missing+=("qemu (qemu-system-x86_64)")
    fi

    if ! command -v qemu-img >/dev/null 2>&1; then
        missing+=("qemu-img")
    fi

    if ! command -v wget >/dev/null 2>&1; then
        missing+=("wget")
    fi

    if ! command -v ssh >/dev/null 2>&1; then
        missing+=("ssh")
    fi

    if [ ${#missing[@]} -ne 0 ]; then
        log_error "Missing required tools:"
        for tool in "${missing[@]}"; do
            echo "  - $tool"
        done
        echo
        echo "Please install the missing tools and run again."
        echo "Ubuntu/Debian: sudo apt install qemu-system-x86 qemu-utils wget openssh-client"
        echo "CentOS/RHEL:   sudo yum install qemu-kvm qemu-img wget openssh-clients"
        echo "macOS:         brew install qemu wget"
        exit 1
    fi

    log_success "All prerequisites found"
}

# Download Alpine Linux ISO
download_alpine() {
    log_step "Downloading Alpine Linux ISO..."

    if [ -f "$ALPINE_ISO" ]; then
        log_info "Alpine ISO already exists: $ALPINE_ISO"
    else
        log_info "Downloading Alpine Linux..."
        wget "$ALPINE_URL" || {
            log_error "Failed to download Alpine Linux ISO"
            exit 1
        }
        log_success "Alpine Linux ISO downloaded"
    fi
}

# Create VM disk
create_vm_disk() {
    log_step "Creating VM disk..."

    if [ -f "$VM_DISK" ]; then
        log_warn "VM disk already exists: $VM_DISK"
        echo -n "Overwrite existing VM? (y/N): "
        read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then
            log_info "Skipping VM disk creation"
            return
        fi
        rm -f "$VM_DISK"
    fi

    qemu-img create -f qcow2 "$VM_DISK" "$VM_SIZE" || {
        log_error "Failed to create VM disk"
        exit 1
    }

    log_success "VM disk created: $VM_DISK ($VM_SIZE)"
}

# Build YakirOS static binaries
build_yakiros() {
    log_step "Building YakirOS static binaries..."

    if [ ! -f "Makefile" ]; then
        log_error "Makefile not found. Run this script from YakirOS source directory."
        exit 1
    fi

    # Clean and build static
    make clean >/dev/null 2>&1 || true
    make static || {
        log_error "Failed to build YakirOS static binaries"
        exit 1
    }

    # Verify static build
    if file graph-resolver | grep -q "statically linked"; then
        log_success "YakirOS static binaries built successfully"
    else
        log_warn "Warning: graph-resolver may not be statically linked"
    fi
}

# Start VM for installation
start_install_vm() {
    log_step "Starting VM for Alpine installation..."

    cat << 'EOF'

📋 ALPINE INSTALLATION INSTRUCTIONS:
===================================

The VM will boot into Alpine Linux installer. Follow these steps:

1. Login as 'root' (no password)
2. Run: setup-alpine
3. Configure as follows:
   - Keyboard layout: us
   - Hostname: yakiros-test
   - Interface: eth0
   - IP address: dhcp
   - Root password: (choose a password)
   - Timezone: (your timezone)
   - Proxy: none
   - SSH server: openssh
   - Disk: sda
   - Use: sys
   - Erase disk: y

4. When installation completes, type: poweroff
5. Press ENTER to continue...

EOF

    read -r

    log_info "Starting VM installer (use Ctrl+A, X to exit QEMU)..."

    qemu-system-x86_64 \
        -m "$VM_RAM" \
        -smp "$VM_CPUS" \
        -enable-kvm \
        -drive file="$VM_DISK",format=qcow2 \
        -cdrom "$ALPINE_ISO" \
        -netdev user,id=net0,hostfwd=tcp::"$SSH_PORT"-:22 \
        -device virtio-net,netdev=net0 \
        -nographic || {
        log_error "VM installation failed"
        exit 1
    }

    log_success "Alpine installation completed"
}

# Create YakirOS installation archive
create_installation_package() {
    log_step "Creating YakirOS installation package..."

    # Create temporary directory structure
    local temp_dir=$(mktemp -d)
    local install_dir="$temp_dir/yakiros-install"

    mkdir -p "$install_dir/bin"
    mkdir -p "$install_dir/config"
    mkdir -p "$install_dir/scripts"

    # Copy binaries
    cp graph-resolver graphctl "$install_dir/bin/"

    # Create component configurations
    cat > "$install_dir/config/00-kernel.toml" << 'EOF'
[component]
name = "kernel"
type = "service"
binary = "/bin/true"

[provides]
capabilities = ["kernel", "early-boot"]
EOF

    cat > "$install_dir/config/05-syslog.toml" << 'EOF'
[component]
name = "syslog"
type = "service"
binary = "/bin/sh"
args = ["-c", "/sbin/syslogd -n & /usr/local/bin/syslog-ready.sh"]

[requires]
capabilities = ["kernel"]

[provides]
capabilities = ["logging"]

[lifecycle]
readiness_file = "/run/syslog.ready"
readiness_timeout = 10
EOF

    cat > "$install_dir/config/10-network.toml" << 'EOF'
[component]
name = "networking"
type = "service"
binary = "/sbin/udhcpc"
args = ["-i", "eth0", "-f", "-S"]

[requires]
capabilities = ["kernel"]

[provides]
capabilities = ["network"]

[lifecycle]
readiness_command = "ping -c 1 -W 2 8.8.8.8"
readiness_timeout = 30
readiness_interval = 5
EOF

    cat > "$install_dir/config/20-sshd.toml" << 'EOF'
[component]
name = "sshd"
type = "service"
binary = "/usr/sbin/sshd"
args = ["-D", "-f", "/etc/ssh/sshd_config"]

[requires]
capabilities = ["network"]

[provides]
capabilities = ["ssh-server"]

[lifecycle]
readiness_command = "netstat -tln | grep :22"
readiness_timeout = 15
readiness_interval = 3
EOF

    cat > "$install_dir/config/25-database.toml" << 'EOF'
[component]
name = "demo-database"
type = "service"
binary = "/usr/local/bin/demo-database.sh"

[requires]
capabilities = ["logging"]

[provides]
capabilities = ["database"]

[lifecycle]
readiness_check = "/usr/local/bin/db-health-check.sh"
readiness_timeout = 20
readiness_interval = 2
EOF

    cat > "$install_dir/config/30-webserver.toml" << 'EOF'
[component]
name = "demo-webserver"
type = "service"
binary = "/usr/local/bin/demo-webserver.sh"

[requires]
capabilities = ["network", "logging"]

[provides]
capabilities = ["http-server"]

[lifecycle]
readiness_file = "/run/webserver.ready"
readiness_timeout = 15
EOF

    # Create helper scripts
    cat > "$install_dir/scripts/syslog-ready.sh" << 'EOF'
#!/bin/sh
sleep 2
touch /run/syslog.ready
EOF

    cat > "$install_dir/scripts/demo-database.sh" << 'EOF'
#!/bin/sh
DB_DIR="/var/lib/demodb"
mkdir -p "$DB_DIR"

echo "Starting demo database..."
sleep 5
echo "Database ready" > "$DB_DIR/status"

while true; do
  sleep 10
  echo "DB heartbeat: $(date)" >> "$DB_DIR/log"
done
EOF

    cat > "$install_dir/scripts/demo-webserver.sh" << 'EOF'
#!/bin/sh
READY_FILE="/run/webserver.ready"

# Start simple HTTP server in background
while true; do
  echo -e "HTTP/1.1 200 OK\r\n\r\n<h1>YakirOS Demo Server</h1><p>Readiness Protocol Working!</p><p>Time: $(date)</p>" | nc -l -p 8080 -q 1 2>/dev/null
done &

# Signal readiness after brief initialization
sleep 3
echo "Web server ready at $(date)" > "$READY_FILE"

wait
EOF

    cat > "$install_dir/scripts/db-health-check.sh" << 'EOF'
#!/bin/sh
test -f /var/lib/demodb/status && grep -q "ready" /var/lib/demodb/status
EOF

    # Create installation script
    cat > "$install_dir/install.sh" << 'EOF'
#!/bin/sh
set -e

echo "🔧 Installing YakirOS..."

# Install binaries
install -d /sbin /usr/bin /etc/graph.d /usr/local/bin
install -m 755 bin/graph-resolver /sbin/
install -m 755 bin/graphctl /usr/bin/

# Install configurations
install -m 644 config/*.toml /etc/graph.d/

# Install helper scripts
install -m 755 scripts/*.sh /usr/local/bin/

# Update boot configuration
sed -i 's/default_kernel_opts=.*/default_kernel_opts="console=ttyS0,115200 init=\/sbin\/graph-resolver"/' /etc/update-extlinux.conf
update-extlinux

echo "✅ YakirOS installation completed"
echo "🚀 System will boot with YakirOS as PID 1 on next reboot"
EOF

    chmod +x "$install_dir/install.sh"
    chmod +x "$install_dir/scripts"/*.sh

    # Create tarball
    tar -czf yakiros-install.tar.gz -C "$temp_dir" yakiros-install

    # Cleanup
    rm -rf "$temp_dir"

    log_success "YakirOS installation package created: yakiros-install.tar.gz"
}

# Boot VM for YakirOS installation
install_yakiros() {
    log_step "Starting VM for YakirOS installation..."

    cat << EOF

📡 SSH CONNECTION INSTRUCTIONS:
==============================

The VM will boot. You can then SSH into it to install YakirOS:

  ssh -p $SSH_PORT root@localhost

Follow the automated installation process shown after SSH connection.

EOF

    # Start VM in background
    log_info "Starting VM (SSH available on port $SSH_PORT)..."

    qemu-system-x86_64 \
        -m "$VM_RAM" \
        -smp "$VM_CPUS" \
        -enable-kvm \
        -drive file="$VM_DISK",format=qcow2 \
        -netdev user,id=net0,hostfwd=tcp::"$SSH_PORT"-:22,hostfwd=tcp::8080-:8080 \
        -device virtio-net,netdev=net0 \
        -nographic &

    local vm_pid=$!
    echo $vm_pid > "${VM_NAME}.pid"

    log_info "VM started with PID: $vm_pid"
    log_info "SSH: ssh -p $SSH_PORT root@localhost"
    log_info "Web: http://localhost:8080 (after YakirOS installation)"

    # Wait for VM to boot
    sleep 15

    # Try to connect and install
    log_step "Attempting automatic YakirOS installation..."

    # Wait for SSH to be ready
    local retries=0
    while [ $retries -lt 30 ]; do
        if ssh -p "$SSH_PORT" -o ConnectTimeout=5 -o StrictHostKeyChecking=no root@localhost "echo 'SSH ready'" >/dev/null 2>&1; then
            break
        fi
        retries=$((retries + 1))
        sleep 2
    done

    if [ $retries -eq 30 ]; then
        log_warn "Could not establish SSH connection for automatic installation"
        log_info "Please manually SSH and install YakirOS:"
        log_info "  ssh -p $SSH_PORT root@localhost"
        return
    fi

    # Transfer and install YakirOS
    log_info "Transferring YakirOS installation package..."
    scp -P "$SSH_PORT" -o StrictHostKeyChecking=no yakiros-install.tar.gz root@localhost:/tmp/ || {
        log_warn "Failed to transfer installation package automatically"
        return
    }

    log_info "Installing YakirOS in VM..."
    ssh -p "$SSH_PORT" -o StrictHostKeyChecking=no root@localhost << 'EOF'
set -e
cd /tmp
tar -xzf yakiros-install.tar.gz
cd yakiros-install
./install.sh
EOF

    log_success "YakirOS installation completed!"
}

# Reboot VM with YakirOS
test_yakiros() {
    log_step "Rebooting VM with YakirOS as PID 1..."

    log_info "Rebooting VM..."
    ssh -p "$SSH_PORT" -o StrictHostKeyChecking=no root@localhost "reboot" 2>/dev/null || true

    # Wait for reboot
    sleep 10

    # Wait for YakirOS to boot
    log_info "Waiting for YakirOS to boot..."
    local retries=0
    while [ $retries -lt 60 ]; do
        if ssh -p "$SSH_PORT" -o ConnectTimeout=5 -o StrictHostKeyChecking=no root@localhost "graphctl status" >/dev/null 2>&1; then
            break
        fi
        retries=$((retries + 1))
        sleep 3
    done

    if [ $retries -eq 60 ]; then
        log_warn "YakirOS may not have started properly"
        log_info "Try connecting manually: ssh -p $SSH_PORT root@localhost"
        return
    fi

    log_success "YakirOS is running as PID 1!"
}

# Show test instructions
show_test_instructions() {
    cat << EOF

🎉 YAKIROS VM SETUP COMPLETED SUCCESSFULLY!
===========================================

Your YakirOS testing environment is ready:

🔗 CONNECTIONS:
   SSH:  ssh -p $SSH_PORT root@localhost
   Web:  http://localhost:8080

🔧 TEST COMMANDS:
   # Check system status with readiness information
   graphctl status

   # View detailed readiness information
   graphctl readiness

   # Manually trigger readiness checks
   graphctl check-readiness database

   # Test web server
   curl http://localhost:8080

   # View component logs
   tail -f /var/log/messages

🧪 WHAT TO TEST:
   ✅ System boots with YakirOS as PID 1
   ✅ Services start in dependency order
   ✅ Readiness protocol prevents premature capability availability
   ✅ All three readiness methods working (file, command, signal)
   ✅ Service restart and failure handling
   ✅ Remote management via graphctl

📊 MONITORING:
   # Watch readiness states during boot
   watch graphctl status

   # Monitor resource usage
   top | grep graph-resolver

   # Test service failures
   pkill demo-webserver  # Watch it restart

🔄 VM MANAGEMENT:
   Stop VM:    kill \$(cat ${VM_NAME}.pid)
   Start VM:   $0 start-vm
   Clean up:   $0 cleanup

🎯 SUCCESS CRITERIA:
   - YakirOS running as PID 1 (check with: ps aux | head -5)
   - All services showing ACTIVE state in graphctl status
   - Web server responding at http://localhost:8080
   - SSH access working for remote management

Happy Testing! 🚀

EOF
}

# Start existing VM
start_vm() {
    if [ ! -f "$VM_DISK" ]; then
        log_error "VM disk not found: $VM_DISK"
        exit 1
    fi

    log_info "Starting existing YakirOS VM..."

    qemu-system-x86_64 \
        -m "$VM_RAM" \
        -smp "$VM_CPUS" \
        -enable-kvm \
        -drive file="$VM_DISK",format=qcow2 \
        -netdev user,id=net0,hostfwd=tcp::"$SSH_PORT"-:22,hostfwd=tcp::8080-:8080 \
        -device virtio-net,netdev=net0 \
        -nographic &

    local vm_pid=$!
    echo $vm_pid > "${VM_NAME}.pid"

    log_success "VM started with PID: $vm_pid"
    log_info "SSH: ssh -p $SSH_PORT root@localhost"
}

# Cleanup function
cleanup() {
    log_step "Cleaning up YakirOS VM environment..."

    # Stop VM if running
    if [ -f "${VM_NAME}.pid" ]; then
        local vm_pid=$(cat "${VM_NAME}.pid")
        if kill -0 "$vm_pid" 2>/dev/null; then
            log_info "Stopping VM (PID: $vm_pid)..."
            kill "$vm_pid" 2>/dev/null || true
            sleep 2
        fi
        rm -f "${VM_NAME}.pid"
    fi

    # Remove files
    local files=("$VM_DISK" "$ALPINE_ISO" "yakiros-install.tar.gz")
    for file in "${files[@]}"; do
        if [ -f "$file" ]; then
            rm -f "$file"
            log_info "Removed: $file"
        fi
    done

    log_success "Cleanup completed"
}

# Main function
main() {
    echo -e "${CYAN}"
    cat << 'EOF'
   ██╗   ██╗ █████╗ ██╗  ██╗██╗██████╗  ██████╗ ███████╗
   ╚██╗ ██╔╝██╔══██╗██║ ██╔╝██║██╔══██╗██╔═══██╗██╔════╝
    ╚████╔╝ ███████║█████╔╝ ██║██████╔╝██║   ██║███████╗
     ╚██╔╝  ██╔══██║██╔═██╗ ██║██╔══██╗██║   ██║╚════██║
      ██║   ██║  ██║██║  ██╗██║██║  ██║╚██████╔╝███████║
      ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝

    Virtual Machine Setup Script - Complete PID 1 Testing
    =====================================================
EOF
    echo -e "${NC}"

    case "${1:-setup}" in
        "setup")
            log_info "Starting YakirOS VM setup process..."
            check_prerequisites
            build_yakiros
            download_alpine
            create_vm_disk
            create_installation_package
            start_install_vm
            log_info "Alpine installation completed. Continuing with YakirOS installation..."
            install_yakiros
            test_yakiros
            show_test_instructions
            ;;
        "start-vm")
            start_vm
            show_test_instructions
            ;;
        "cleanup")
            cleanup
            ;;
        "help"|"-h"|"--help")
            echo "Usage: $0 [command]"
            echo "Commands:"
            echo "  setup     - Full VM setup and YakirOS installation (default)"
            echo "  start-vm  - Start existing VM"
            echo "  cleanup   - Remove all VM files"
            echo "  help      - Show this help"
            ;;
        *)
            log_error "Unknown command: $1"
            echo "Use '$0 help' for usage information"
            exit 1
            ;;
    esac
}

# Handle Ctrl+C gracefully
trap 'log_warn "Setup interrupted by user"; cleanup; exit 1' INT

# Run main function
main "$@"