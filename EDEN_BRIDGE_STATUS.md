# Eden Rooms PS5 — instalador con Eden Bridge (prototipo)

El archivo `build/EdenRooms-Installer.elf` registra/actualiza el acceso directo **Eden Rooms PS5** en Multimedia y, **en la misma ejecución**, inicia un servidor local de prueba en `127.0.0.1:24879`.

## Estado real y limitaciones

- El servidor **solo funciona mientras el proceso ELF siga ejecutándose**. No se instala como servicio persistente, ni se inicia automáticamente al encender la consola.
- El navegador PS5 podría bloquear las peticiones de la web HTTPS hacia HTTP loopback (contenido mixto o restricciones de red privada). **No está probado en hardware**.
- La escritura de configuración se implementó como prototipo. **No lo uses todavía sobre una configuración sin respaldo adicional**.
- El icono personalizado elegido **todavía no se incorporó** al binario; el repositorio mantiene `assets/icon0.png` anterior.
- La compilación de GitHub Actions debe verificarse antes de instalar; el repositorio no garantiza aún un ELF funcional.

## Operación prevista

1. Ejecutar `EdenRooms-Installer.elf` desde el cargador de payloads.
2. Mantener activo ese proceso mientras se abre Eden Rooms PS5 desde Multimedia.
3. Seleccionar una sala y pulsar **Enviar a ProsperoEden**.
4. El servidor recibe `POST /v1/connection`, cuerpo `{"host":"192.0.2.10","port":24872}` y `Origin: https://eden-rooms-ps5.pages.dev`.
5. Si se puede acceder a `/data/prosperoeden/config/prosperoeden.json`, se guardan `host` y `port` en cada objeto `multiplayer` encontrado, conservando las demás claves. Se crea un respaldo `prosperoeden.json.edenrooms.bak`.
6. Abrir ProsperoEden **después de haber cerrado su proceso** para que lea los nuevos valores.

## Riesgos y pendientes

- El prototipo no comprueba que ProsperoEden esté cerrado, por lo que una configuración guardada por el emulador podría sobrescribir el cambio.
- La actualización JSON es textual, no utiliza un analizador JSON completo. No usar hasta validar con copias representativas y comprobar la estructura.
- La protección de origen evita solicitudes de otros sitios web en navegadores que envían `Origin`, pero **no autentica clientes locales que puedan falsificar cabeceras**.
- Faltan pruebas de permisos de archivo, ejecución simultánea de navegador y payload, compatibilidad HTTPS→loopback, manejo de perfiles y restauración ante errores.
- No está garantizado que el icono se refresque inmediatamente después de actualizar la app.

## Desarrollo

`make all` compila el ELF y el PKG. **El PKG solo instala el acceso directo; no ejecuta Eden Bridge**, porque el `eboot.bin` del acceso directo es vacío. Para el prototipo de Eden Bridge debe usarse el ELF.

El código del servidor está en `src/eden_bridge.c`; el instalador en `src/app_installer.c`.
