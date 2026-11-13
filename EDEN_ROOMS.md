# Eden Rooms PS5 — instalador para Multimedia

Este fork adapta **PS5 Browser Search** para crear un acceso directo llamado **Eden Rooms PS5** en la pestaña **Multimedia** de una PS5 liberada.

- Destino: `https://eden-rooms-ps5.pages.dev/`
- ID propio: `EDEN00001` (no sobrescribe el acceso directo original `BRWS00001`)
- Payload instalador: `build/EdenRooms-Installer.elf`
- También genera un PKG de instalación.
- Conserva por ahora el icono heredado del proyecto original: **pendiente de reemplazar por el logo Eden Rooms**.

## Compilación en GitHub

Abrí **Actions → Build Eden Rooms PS5 installer → Run workflow** y, si la ejecución termina correctamente, descargá **EdenRooms-PS5-installers** de Artifacts.

El flujo construye un contenedor Docker con el SDK y .NET; la primera compilación puede tardar bastante.

## Instalación

Con la PS5 liberada, ejecutá `EdenRooms-Installer.elf` desde tu cargador de payloads. El instalador registra el acceso directo en Multimedia. Como alternativa, instalá el PKG.

**Importante:** el dominio `eden-rooms-ps5.pages.dev` debe publicarse en Cloudflare Pages antes de usar el acceso directo. El instalador no crea ni despliega esa web.

## Conexión directa con ProsperoEden

Esta versión **solo instala el acceso directo web**. Todavía **no** ejecuta un servicio local ni modifica `/data/prosperoeden/config/prosperoeden.json`.

La futura integración deberá recibir IP/puerto desde la web, validarlos, requerir autorización local, crear copia de seguridad y actualizar únicamente `multiplayer.host` y `multiplayer.port` en la configuración general y el perfil activo. Debe realizarse con ProsperoEden cerrado y sin exponer una API de escritura a Internet.

Basado en [kvnhrt/ps5-browser-app](https://github.com/kvnhrt/ps5-browser-app), GPL-3.0.
