# Maintainer: Your Name <your@email.com>
pkgname=dualsense-linux
pkgver=0.1.0
pkgrel=1
pkgdesc='DualSense adaptive triggers, rumble, haptics, and LED control over BT/USB on Linux'
arch=('x86_64')
url='https://github.com/youruser/dualsense-linux'
license=('MIT')
depends=('glibc')
makedepends=('cmake')
optdepends=(
    'python-gobject: GUI support'
    'gtk4: GUI support'
    'libadwaita: GUI support'
    'pipewire: BT audio haptics'
    'socat: daemon testing'
)
backup=('etc/udev/rules.d/99-dualsense.rules')
source=()

build() {
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        "$startdir"
    cmake --build build
}

package() {
    DESTDIR="$pkgdir" cmake --install build

    # GUI
    install -Dm755 "$startdir/gui/dualsense-gui.py" \
        "$pkgdir/usr/bin/dualsense-gui"

    # Profiles
    install -dm755 "$pkgdir/usr/share/dualsense/profiles"
    install -Dm644 "$startdir/profiles/"*.json \
        -t "$pkgdir/usr/share/dualsense/profiles/"

    # Python bindings
    local pydir="$pkgdir/usr/lib/python3/dist-packages"
    install -dm755 "$pydir"
    install -Dm644 "$startdir/bindings/python/dualsense.py" \
        "$pydir/dualsense.py"

    # License
    install -Dm644 "$startdir/README.md" \
        "$pkgdir/usr/share/doc/$pkgname/README.md"

    # Systemd user service
    install -Dm644 "$startdir/daemon/dualsensed.service" \
        "$pkgdir/usr/lib/systemd/user/dualsensed.service"
}
