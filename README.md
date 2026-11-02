# NixDE

NixDE es un entorno de escritorio Wayland basado en wlroots, con compositor y
panel GTK propios.

## Instalar desde el repositorio de pacman

El repositorio se publica en GitHub Pages para x86_64. Una vez habilitado Pages
con GitHub Actions en el repositorio de GitHub, añade esta sección a
`/etc/pacman.conf`:

```ini
[nixde]
SigLevel = Optional
Server = https://ralcommuny.github.io/NixDE/$arch
```

Después actualiza el sistema e instala NixDE:

```sh
doas pacman -Syu nixde
```

Pacman instala automáticamente las dependencias declaradas: wlroots 0.20.2 o
posterior, GTK 4, Foot, Fuzzel y systemd. SDDM es opcional; cualquier gestor
compatible con sesiones Wayland puede iniciar NixDE. Selecciona **NixDE
(Wayland)** en el gestor de acceso.

La firma de paquetes está pendiente de configurar. `SigLevel = Optional` permite
instalar paquetes sin firma mientras tanto; úsalo solo si confías en este
repositorio y cámbialo a una política de firma requerida cuando se configure
una clave GPG.

## Publicar una versión

El workflow `.github/workflows/publish-arch-repo.yml` construye el paquete y
publica el paquete y el índice de pacman en GitHub Pages al enviar una etiqueta
`v*`. En la configuración del repositorio de GitHub, habilita **Pages** con
**GitHub Actions** como origen.

Antes de publicar una versión nueva:

1. Actualiza `pkgver`, `_commit` y `sha256sums` en `PKGBUILD` para que apunten
   a la versión de código que quieras publicar.
2. Envía esos cambios a GitHub.
3. Crea y envía la etiqueta correspondiente, por ejemplo `v0.1.0`.

El paquete actual está fijado al commit que indica `_commit`; por eso, publicar
este workflow no publica automáticamente cambios locales que aún no estén en
GitHub. La construcción requiere Arch Linux y no puede ejecutarse con `makepkg`
en sistemas que no sean Arch.

El workflow también se puede ejecutar manualmente desde la pestaña **Actions**.
El índice publicado contiene un paquete x86_64.

## Compilar desde el código fuente

En Arch Linux, instala las herramientas de compilación y dependencias:

```sh
doas pacman -S --needed base-devel meson ninja pkgconf wlroots0.20 gtk4 \
  wayland-protocols foot fuzzel
```

Para compilar e instalar directamente:

```sh
meson setup --prefix=/usr build
meson compile -C build
doas meson install -C build
```
