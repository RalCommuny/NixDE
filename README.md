# NixDE

NixDE es una base de entorno de escritorio para Arch Linux: un compositor y
gestor de ventanas Wayland propio, basado en wlroots, más un panel GTK propio.
No inicia Sway, Hyprland ni otro gestor de ventanas.

## Funciones actuales

- Sesión Wayland seleccionable desde SDDM.
- Ventanas nativas Wayland, foco por clic y movimiento con `Alt` + arrastrar.
- `Alt+Enter`: abrir Foot.
- `Alt+Espacio`: abrir el lanzador Fuzzel.
- `Alt+Tab`: cambiar de ventana.
- `Alt+F4`: solicitar el cierre de la ventana enfocada.
- `Alt+Shift+Q`: cerrar la sesión del compositor y volver al gestor de acceso.
- Panel NixDE en la parte inferior, con el logo, un lanzador propio con búsqueda,
  acceso rápido a la terminal, reloj y menú de energía.
- Lista de tareas que muestra las ventanas abiertas; pulsa un botón para volver
  a esa ventana o usa el clic derecho para solicitar su cierre. El botón activo
  se resalta. El lanzador incluye búsqueda y muestra las aplicaciones instaladas
  que publican un acceso `.desktop`, con el icono de cada aplicación en su botón
  de tarea.
- Panel fijo, sin bordes ni controles de ventana, a lo ancho de la pantalla;
  se mantiene anclado abajo al cambiar la configuración de pantallas, no se
  puede mover con `Alt`, cerrar con `Alt+F4` ni se lista como aplicación abierta.

El panel se ejecuta como cliente GTK y el compositor lo reconoce por su ID
`org.nixde.Panel`, lo coloca abajo y expone las ventanas abiertas al panel por un
socket local privado.
El soporte actual se centra en una pantalla y aplicaciones nativas Wayland; todavía
no incluye XWayland, layer-shell de terceros, configuración gráfica de monitores
ni un centro de ajustes.

## Instalar desde el repositorio de pacman

El repositorio se publica en GitHub Pages para x86_64. En GitHub, habilita
**Settings → Pages → GitHub Actions**. Después ejecuta
**Actions → Publish Arch repository → Run workflow** y espera a que termine
correctamente.

Añade esta sección a `/etc/pacman.conf`:

```ini
[nixde]
SigLevel = Optional
Server = https://ralcommuny.github.io/NixDE/$arch
```

Actualiza las bases de datos e instala el paquete:

```sh
doas pacman -Syu nixde
```

Pacman instala las dependencias necesarias: wlroots 0.20.2, GTK 4, Foot, Fuzzel
y systemd. SDDM es opcional; se puede usar cualquier gestor compatible con
sesiones Wayland.

La firma de paquetes aún no está configurada. `SigLevel = Optional` permite
instalar paquetes sin firma; úsalo solo si confías en este repositorio. Cuando
se configure una clave GPG, se debe distribuir la clave pública y exigir la
firma en esta sección.

El workflow también se ejecuta al enviar una etiqueta `v*`. Para publicar
cambios posteriores, sube primero el código a GitHub y actualiza `pkgver`,
`pkgrel`, `_commit` y `sha256sums` en `PKGBUILD` para que apunten a esa versión.
Después ejecuta el workflow manualmente o envía una etiqueta de versión.

## Dependencias

Para compilar en Arch Linux:

```sh
doas pacman -S --needed base-devel meson ninja pkgconf wlroots0.20 gtk4 \
  wayland-protocols foot fuzzel
```

El proyecto requiere wlroots 0.20.x. En Arch, la dependencia pkg-config se llama
`wlroots-0.20`.

## Compilar e instalar

Desde la raíz del proyecto, ejecuta:

```sh
meson setup --prefix=/usr build
meson compile -C build
doas meson install -C build
```

El prefijo `/usr` instala la sesión donde SDDM la busca:
`/usr/share/wayland-sessions/nixde.desktop`, y el logo en los iconos del sistema.
En la pantalla de acceso, abre el selector de sesión, elige **NixDE (Wayland)** e
inicia sesión.

## Apariencia del panel

El estilo predeterminado está en `data/nixde-panel.css` y se instala como
`/usr/share/nixde/panel.css`. Para personalizarlo sin editar los archivos del
sistema, crea `~/.config/nixde/panel.css`. El panel detecta cambios y aplica el
CSS al guardar; reinicia la sesión si el archivo se acaba de crear y no se
actualiza.

Clases útiles para personalizar el panel con GTK CSS:

```css
.nixde-panel { background-color: #20242c; }
.nixde-launcher-button { background-color: #303846; }
.nixde-task-button-active { border-bottom-color: #38bdf8; }
.nixde-clock { color: #a5f3fc; }
```

También están disponibles `.nixde-panel-content`, `.nixde-task-list`,
`.nixde-task-button`, `.nixde-launcher`, `.nixde-launcher-search`,
`.nixde-app-list`, `.nixde-system-menu`, `.nixde-task-context-popover` y
`.nixde-close-window-button`.

El CSS personaliza el panel y sus menús; no redondea las ventanas de otras
aplicaciones. NixDE aún no implementa el recorte de esquinas en el compositor,
que es necesario para aplicar ese efecto de forma uniforme a todas las ventanas.

Para actualizar una instalación existente después de cambiar el código, ejecuta
desde la carpeta del proyecto:

```sh
meson compile -C build
doas meson install -C build
```

Después cierra la sesión y vuelve a entrar en **NixDE (Wayland)** para que SDDM
inicie el compositor y el panel actualizados.

Si SDDM todavía no está activo, habilítalo con `doas systemctl enable --now sddm`.
Hazlo desde una TTY y solo si quieres usar SDDM como gestor de acceso; no lo
hagas mientras otro gestor de acceso esté activo.

Para empaquetar el proyecto localmente, ejecuta `makepkg -si` desde el directorio
del proyecto.

## Teclas

| Atajo | Acción |
|---|---|
| `Alt+Enter` | Terminal |
| `Alt+Espacio` | Lanzador de aplicaciones |
| `Alt+Tab` | Cambiar de ventana |
| `Alt+F4` | Cerrar ventana enfocada |
| `Alt` + botón izquierdo y arrastrar | Mover ventana |
| `Alt+Shift+Q` | Salir del escritorio |
