pkgname=nixde
pkgver=0.1.0
pkgrel=1
pkgdesc='Entorno de escritorio Wayland propio basado en wlroots'
arch=('x86_64')
license=('MIT')
_commit=ac088b72d1d25e44b39ebd06799f5ae49ad16e6f
depends=('wlroots0.20>=0.20.2' 'gtk4' 'foot' 'fuzzel' 'systemd')
optdepends=('sddm: display manager for selecting the NixDE session')
makedepends=('meson' 'ninja' 'pkgconf')
source=("$pkgname-$pkgver.tar.gz::https://github.com/RalCommuny/NixDE/archive/$_commit.tar.gz")
sha256sums=('2e81f8a2cc6f1b998f52100502cfe9f735926f1a686626153a5e0ccbbc719b07')

build() {
  meson setup --prefix=/usr "$srcdir/$pkgname-build" \
    "$srcdir/NixDE-$_commit"
  meson compile -C "$srcdir/$pkgname-build"
}

package() {
  DESTDIR="$pkgdir" meson install -C "$srcdir/$pkgname-build"
  install -Dm644 "$srcdir/NixDE-$_commit/LICENSE" \
    "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
