#!/bin/bash
#
# YakirOS Step 11: VM Integration Testing Setup
# ============================================
#
# Comprehensive VM testing environment for all YakirOS advanced features:
# - Hot-swap services (Step 4)
# - Health checks (Step 6)
# - Isolation/cgroups (Step 7)
# - Cycle detection (Step 8)
# - CRIU checkpointing (Step 9)
# - kexec live upgrades (Step 10)
#

set -e

# Enhanced VM Configuration for comprehensive testing
VM_NAME="yakiros-step11-vm"
VM_DISK="${VM_NAME}.qcow2"
VM_SIZE="20G"         # Increased for CRIU checkpoints and logs
VM_RAM="4096"         # 4GB for CRIU and isolation testing
VM_CPUS="4"           # Multi-core for performance testing
SSH_PORT="2222"
HTTP_PORT="8080"
HTTP_TEST_PORT="8081"
ALPINE_ISO="alpine-virt-3.19.1-x86_64.iso"
ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/${ALPINE_ISO}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

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
    exit 1
}

log_header() {
    echo -e "\n${PURPLE}🚀 $1${NC}"
    echo "================================================================="
}

# Check prerequisites with enhanced requirements
check_prerequisites() {
    log_header "Checking Prerequisites"

    # Check QEMU
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        log_error "qemu-system-x86_64 not found. Install with: sudo apt install qemu-system-x86"
    fi
    log_success "QEMU found: $(qemu-system-x86_64 --version | head -1)"

    # Check system resources
    local available_ram=$(free -m | awk 'NR==2{print $7}')
    if [ "$available_ram" -lt 5000 ]; then
        log_warn "Available RAM: ${available_ram}MB (recommended: 5GB+ for comprehensive testing)"
    else
        log_success "Available RAM: ${available_ram}MB"
    fi

    # Check disk space
    local available_disk=$(df . | awk 'NR==2{print int($4/1024/1024)}')
    if [ "$available_disk" -lt 25 ]; then
        log_warn "Available disk: ${available_disk}GB (recommended: 25GB+ for testing)"
    else
        log_success "Available disk: ${available_disk}GB"
    fi

    # Check for KVM acceleration
    if [ -r /dev/kvm ]; then
        log_success "KVM acceleration available"
        KVM_ARGS="-enable-kvm"
    else
        log_warn "KVM not available (will use software emulation - slower)"
        KVM_ARGS=""
    fi

    # Verify YakirOS build
    if [ ! -f "../../graph-resolver" ]; then
        log_error "YakirOS not built. Run 'make' in project root first."
    fi
    log_success "YakirOS binaries found"

    # Verify test components exist
    if [ ! -d "yakiros-step11-config" ]; then
        log_error "Test components not found. Run this script from tests/vm/ directory."
    fi
    log_success "Test components found"
}

# Download Alpine Linux if needed
download_alpine() {
    if [ ! -f "${ALPINE_ISO}" ]; then
        log_header "Downloading Alpine Linux"
        log_info "Downloading ${ALPINE_ISO}..."
        wget -O "${ALPINE_ISO}" "${ALPINE_URL}"
        log_success "Alpine Linux downloaded"
    else
        log_success "Alpine Linux ISO already exists"
    fi
}

# Create VM disk image
create_vm_disk() {
    if [ ! -f "${VM_DISK}" ]; then
        log_header "Creating VM Disk Image"
        log_info "Creating ${VM_SIZE} disk image: ${VM_DISK}"
        qemu-img create -f qcow2 "${VM_DISK}" "${VM_SIZE}"
        log_success "VM disk created"
    else
        log_success "VM disk already exists"
    fi
}

# Generate comprehensive Alpine Linux installation script
generate_install_script() {
    log_header "Generating Enhanced Installation Script"

    cat > alpine-install.sh << 'EOF'
#!/bin/sh
set -e

# Setup Alpine repositories for additional packages
echo "http://dl-cdn.alpinelinux.org/alpine/v3.19/main" > /etc/apk/repositories
echo "http://dl-cdn.alpinelinux.org/alpine/v3.19/community" >> /etc/apk/repositories
echo "http://dl-cdn.alpinelinux.org/alpine/edge/testing" >> /etc/apk/repositories

# Update package index
apk update

# Install comprehensive package set for testing
apk add --no-cache \
    openssh \
    bash \
    htop \
    curl \
    wget \
    strace \
    tcpdump \
    netstat-nat \
    lsof \
    procps \
    util-linux \
    coreutils \
    findutils \
    grep \
    sed \
    awk \
    tar \
    gzip \
    python3 \
    py3-pip \
    jq \
    socat \
    nc-openbsd \
    iproute2 \
    iptables \
    criu || echo "CRIU not available in this Alpine version"

# Setup root user for SSH access
echo 'root:yakiros-test-vm' | chpasswd

# Configure SSH
sed -i 's/#PermitRootLogin.*/PermitRootLogin yes/' /etc/ssh/sshd_config
sed -i 's/#PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config

# Install YakirOS
echo "Installing YakirOS..."
mkdir -p /sbin /usr/bin /etc/graph.d /run/graph /var/lib/graph /var/log/graph

# Create mount points and directories
mkdir -p /proc /sys /dev /dev/pts /run /var /tmp
mkdir -p /run/graph/checkpoints /var/lib/graph/checkpoints

# Set up cgroup v2 for isolation testing
echo "cgroup2 /sys/fs/cgroup cgroup2 rw,nosuid,nodev,noexec,relatime 0 0" >> /etc/fstab
mkdir -p /sys/fs/cgroup

# Enable SSH service
rc-update add sshd default

# Configure kernel parameters for YakirOS
echo 'GRUB_CMDLINE_LINUX_DEFAULT="console=tty0 console=ttyS0,115200 init=/sbin/graph-resolver yakiros.test=1"' > /etc/default/grub

echo "Alpine installation complete!"
EOF

    log_success "Installation script generated"
}

# Launch VM with installation
install_vm() {
    log_header "Installing Alpine Linux in VM"

    log_info "Starting VM installation process..."
    log_warn "This will take 5-10 minutes. The VM will:"
    log_warn "1. Boot Alpine Linux installer"
    log_warn "2. Install system with YakirOS integration"
    log_warn "3. Install comprehensive testing tools"
    log_warn "4. Configure SSH access and networking"

    # Start VM for installation
    timeout 600 qemu-system-x86_64 \
        -m "${VM_RAM}" \
        -smp "${VM_CPUS}" \
        $KVM_ARGS \
        -drive file="${VM_DISK}",format=qcow2 \
        -cdrom "${ALPINE_ISO}" \
        -netdev user,id=net0,hostfwd=tcp::${SSH_PORT}-:22,hostfwd=tcp::${HTTP_PORT}-:8080,hostfwd=tcp::${HTTP_TEST_PORT}-:8081 \
        -device virtio-net,netdev=net0 \
        -nographic \
        -boot d || {
        log_error "VM installation timed out or failed. Check VM console output."
    }

    log_success "VM installation completed"
}

# Copy YakirOS binaries and configuration to VM
install_yakiros() {
    log_header "Installing YakirOS in VM"

    # Wait for VM to be accessible
    log_info "Waiting for VM to be accessible via SSH..."
    local attempts=0
    while ! ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 -p ${SSH_PORT} root@localhost echo "VM accessible" 2>/dev/null; do
        sleep 5
        attempts=$((attempts + 1))
        if [ $attempts -gt 30 ]; then
            log_error "VM not accessible after 2.5 minutes"
        fi
        log_info "Attempt $attempts: Waiting for SSH access..."
    done
    log_success "VM is accessible"

    # Copy YakirOS binaries
    log_info "Copying YakirOS binaries..."
    scp -o StrictHostKeyChecking=no -P ${SSH_PORT} \
        ../../graph-resolver \
        ../../graphctl \
        root@localhost:/tmp/

    # Copy test binaries
    log_info "Copying test binaries..."
    scp -o StrictHostKeyChecking=no -P ${SSH_PORT} -r \
        test-binaries/* \
        root@localhost:/tmp/

    # Copy test configurations
    log_info "Copying test configurations..."
    scp -o StrictHostKeyChecking=no -P ${SSH_PORT} -r \
        yakiros-step11-config/* \
        root@localhost:/tmp/

    # Install everything in VM
    ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@localhost << 'EOF'
        echo "Installing YakirOS in VM..."

        # Install binaries
        cp /tmp/graph-resolver /sbin/
        cp /tmp/graphctl /usr/bin/
        chmod +x /sbin/graph-resolver /usr/bin/graphctl

        # Install test binaries
        mkdir -p /usr/local/bin
        cp /tmp/test-* /usr/local/bin/
        chmod +x /usr/local/bin/test-*

        # Install component configurations
        cp /tmp/*.toml /etc/graph.d/

        # Create log directories
        mkdir -p /var/log/graph /run/graph

        # Set up cgroup v2 if not already mounted
        if ! mountpoint -q /sys/fs/cgroup; then
            mount -t cgroup2 none /sys/fs/cgroup || echo "cgroup2 mount failed (may not be supported)"
        fi

        # Verify installation
        echo "Verifying YakirOS installation..."
        /sbin/graph-resolver --version || echo "Version check failed"
        /usr/bin/graphctl --help | head -5
        ls -la /etc/graph.d/

        echo "YakirOS installation in VM complete!"
EOF

    log_success "YakirOS installed in VM"
}

# Start VM for testing
start_vm_testing() {
    log_header "Starting VM for Testing"

    log_info "Starting YakirOS VM with comprehensive testing environment..."

    # Start VM in background
    nohup qemu-system-x86_64 \
        -m "${VM_RAM}" \
        -smp "${VM_CPUS}" \
        $KVM_ARGS \
        -drive file="${VM_DISK}",format=qcow2 \
        -netdev user,id=net0,hostfwd=tcp::${SSH_PORT}-:22,hostfwd=tcp::${HTTP_PORT}-:8080,hostfwd=tcp::${HTTP_TEST_PORT}-:8081 \
        -device virtio-net,netdev=net0 \
        -nographic \
        -boot c > vm-console.log 2>&1 &

    echo $! > yakiros-step11-vm.pid

    log_success "VM started with PID $(cat yakiros-step11-vm.pid)"
    log_info "VM console output: tail -f vm-console.log"

    # Wait for VM to be accessible
    log_info "Waiting for YakirOS to start in VM..."
    local attempts=0
    while ! ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 -p ${SSH_PORT} root@localhost /usr/bin/graphctl status 2>/dev/null; do
        sleep 10
        attempts=$((attempts + 1))
        if [ $attempts -gt 20 ]; then
            log_error "YakirOS not responding after 3+ minutes. Check vm-console.log"
        fi
        log_info "Attempt $attempts: Waiting for YakirOS to start..."
    done

    log_success "YakirOS is running in VM!"
}

# Show connection information
show_connection_info() {
    log_header "VM Testing Environment Ready!"

    echo -e "${GREEN}🎯 Connection Information:${NC}"
    echo "  SSH:      ssh -p ${SSH_PORT} root@localhost"
    echo "  Password: yakiros-test-vm"
    echo "  HTTP:     http://localhost:${HTTP_PORT}"
    echo "  Test HTTP: http://localhost:${HTTP_TEST_PORT}"
    echo
    echo -e "${BLUE}🧪 Test Commands (run in VM):${NC}"
    echo "  graphctl status                    # View all components"
    echo "  graphctl capabilities              # View capabilities"
    echo "  graphctl upgrade test-echo-server  # Test hot-swap"
    echo "  graphctl check-cycles              # Test cycle detection"
    echo "  graphctl checkpoint test-stateful  # Test CRIU (if available)"
    echo "  ./run-comprehensive-tests.sh       # Run all automated tests"
    echo
    echo -e "${PURPLE}🔧 VM Management:${NC}"
    echo "  VM PID:   $(cat yakiros-step11-vm.pid 2>/dev/null || echo 'Not running')"
    echo "  Console:  tail -f vm-console.log"
    echo "  Stop VM:  kill \$(cat yakiros-step11-vm.pid)"
    echo
    echo -e "${CYAN}📊 Advanced Testing Available:${NC}"
    echo "  - Hot-swap services with zero downtime"
    echo "  - Health monitoring and degraded states"
    echo "  - cgroup isolation and resource limits"
    echo "  - Dependency cycle detection"
    echo "  - CRIU checkpoint/restore (if available)"
    echo "  - kexec kernel upgrade simulation"
    echo "  - Performance benchmarking"
    echo
    echo -e "${GREEN}✨ Step 11: VM Integration Testing Environment Created!${NC}"
}

# Main execution
main() {
    case "${1:-setup}" in
        setup)
            check_prerequisites
            download_alpine
            create_vm_disk
            generate_install_script
            install_vm
            install_yakiros
            start_vm_testing
            show_connection_info
            ;;
        start-vm)
            if [ -f yakiros-step11-vm.pid ] && kill -0 $(cat yakiros-step11-vm.pid) 2>/dev/null; then
                log_warn "VM already running with PID $(cat yakiros-step11-vm.pid)"
            else
                start_vm_testing
                show_connection_info
            fi
            ;;
        stop-vm)
            if [ -f yakiros-step11-vm.pid ]; then
                kill $(cat yakiros-step11-vm.pid) 2>/dev/null || true
                rm -f yakiros-step11-vm.pid
                log_success "VM stopped"
            else
                log_warn "VM not running"
            fi
            ;;
        cleanup)
            if [ -f yakiros-step11-vm.pid ]; then
                kill $(cat yakiros-step11-vm.pid) 2>/dev/null || true
                rm -f yakiros-step11-vm.pid
            fi
            rm -f "${VM_DISK}" "${ALPINE_ISO}" alpine-install.sh vm-console.log
            log_success "VM environment cleaned up"
            ;;
        status)
            if [ -f yakiros-step11-vm.pid ] && kill -0 $(cat yakiros-step11-vm.pid) 2>/dev/null; then
                echo "VM Status: Running (PID $(cat yakiros-step11-vm.pid))"
                ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@localhost /usr/bin/graphctl status 2>/dev/null || echo "YakirOS not responding"
            else
                echo "VM Status: Not running"
            fi
            ;;
        help|*)
            echo "YakirOS Step 11: VM Integration Testing"
            echo "Usage: $0 [command]"
            echo
            echo "Commands:"
            echo "  setup     - Full VM setup (default)"
            echo "  start-vm  - Start existing VM"
            echo "  stop-vm   - Stop running VM"
            echo "  status    - Show VM status"
            echo "  cleanup   - Remove all VM files"
            echo "  help      - Show this help"
            ;;
    esac
}

# Ensure we're in the right directory
if [ ! -d "yakiros-step11-config" ] && [ ! -d "test-binaries" ]; then
    log_error "Must be run from tests/vm/ directory with test components present"
fi

main "$@"