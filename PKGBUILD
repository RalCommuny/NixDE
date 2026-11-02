pkgname=nixde
pkgver=0.1.0
pkgrel=1
pkgdesc='Entorno de escritorio Wayland propio basado en wlroots'
arch=('x86_64' 'aarch64')
license=('MIT')
depends=('wlroots0.20>=0.20.2' 'wlroots0.20<0.21' 'gtk4' 'foot' 'fuzzel' 'systemd')
makedepends=('meson' 'ninja' 'pkgconf' 'wayland-protocols')
source=()
sha256sums=()

prepare() {
  install -d "$srcdir/nixde"
  cp -a "$startdir/meson.build" "$startdir/src" "$startdir/data" \
    "$startdir/LICENSE" "$srcdir/nixde/"
}

build() {
  cd "$srcdir/nixde"
  meson setup --prefix=/usr build
  meson compile -C build
}

package() {
  cd "$srcdir/nixde"
  DESTDIR="$pkgdir" meson install -C build
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
