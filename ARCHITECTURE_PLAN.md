# Revisión de arquitectura — `feat/memory-battery-architecture`

Estado: **Puntos 1 y 2 cerrados, Punto 3 pendiente de decisión**.
Sustituye por completo un plan anterior que escribí contra `main`, que
resultó estar 46 commits por detrás de esta rama y del trabajo real
del proyecto (ver aviso al final).

## Veredicto

Esta rama **ya cumple, en lo esencial, Clean Architecture y los
principios SOLID** para los límites que más importan: dominio
independiente de LVGL, dominio independiente de persistencia, UI
notificada por puertos en vez de dependencias directas, OCP vía tablas
declarativas, y una batería de 20 tests nativos + un test de pureza de
cabecera que lo verifica en CI. No hace falta un refactor de 10 fases.
Lo que queda es una lista corta de detalles, todos de bajo riesgo.

Verificado en esta sesión, no asumido:
- `make -C sim test` → **20/20 binarios de test pasan** (vida,
  eliminación, daño de comandante, Table Sync, autosave de prefs,
  10 minigames, navegación del registro de pantallas...).
- `make -C sim test-game-state-purity` → `game_state.h` compila y
  ejecuta lógica real **sin ninguna ruta a LVGL**, ni siquiera
  `-DLV_CONF_INCLUDE_SIMPLE`.
- `make firmware` → compila limpio con `arduino-cli` para el ESP32-S3.
- Flasheado en el dispositivo físico conectado.

## Checklist SOLID, con evidencia

| # | Principio | Estado | Evidencia |
|---|---|---|---|
| D1 | DIP — dominio no depende de NVS | ✅ | `game_state.c` incluye `prefs_table.h`/`prefs_roster.h` (declaraciones puras, sin `nvs.h`); cero `nvs_*` en `game_state.c` (verificado por grep). La escritura además está invertida vía `prefs_set_scheduler()` — `prefs.c` no conoce LVGL, es quien lo instala (`prefs_autosave.c`) quien decide cuándo. |
| D2 | DIP — dominio no depende de LVGL/UI | ✅ | `game_hooks.h`/`.c`: struct de punteros a función que sustituye los `extern void refresh_player_ui(void)` de antes — el mismo patrón que yo había empezado a construir en mi Fase 2 sobre `main`, aquí ya completo y con más cobertura (también arma/desarma los 3 `lv_timer_t` del preview, del flash y de la ruleta). Cero `lv_` real en `game_state.c` (los 4 matches de grep son menciones en comentarios). |
| D3 | DIP (menor) — dominio no depende de hardware concreto | ⚠️ | `game_state.c:991-992` llama a `esp_random()` directo (cabecera real de ESP-IDF en firmware, stub en el simulador). Es la única dependencia de plataforma que no pasa por un puerto. Impacto bajo: un generador de números aleatorios no tiene un "comportamiento distinto" que ocultar entre firmware y test, así que no compromete los tests — pero para pureza completa debería ser un hook más en `game_hooks_t`. |
| O1 | OCP — añadir un contador no toca la eliminación | ✅ | `counter_definitions[]` en `game_state.c`, tabla declarativa con `string_id_t` + flags, igual que ya proponía mi plan original. |
| O2 | OCP — añadir una pantalla no toca `knob.c` | ✅ | `screen_registry[]` en `knob.c` (línea 531): dispatch de back-navigation, menu-facing y arranque, los tres por tabla. Exactamente mi Fase 7, ya hecha. |
| I1 | ISP — cabeceras por responsabilidad, no un cajón de sastre | ✅ | `prefs.h` se parte en `prefs_display.h`/`prefs_table.h`/`prefs_roster.h`/`prefs_network.h`/`prefs_scores.h`. `settings.c` (antes 814 líneas, un solo fichero) se partió en `nav.c`, `toast.c`, `ota_notice.c`, `home.c`, `ui_partners.c`, `ui_table_sync.c`, `ui_language.c`, `minigames_menu.c`, `ui_battery.c`, `quad_screen.c`. `ui_mp.c` (1278→705 líneas) se partió en `mp_layout.c`/`mp_victory.c`/`mp_attack_gesture.c`. |
| I2 | ISP — `types.h` deja de ser cajón de sastre | ✅ | `game_types.h` (puro) se separó de `types.h` (LVGL); `types.h` reincluye `game_types.h` para no romper a nadie, exactamente el patrón que yo usé para `core/life.h` en mi Fase 1. |
| S1/S2 | SRP — módulos con una responsabilidad | 🟡 | `game_state.c` (1214 líneas) agrupa vida, selección, contadores, deshacer, animación de selección Y la implementación de Table Sync/`net_sync_*`, en 10 secciones marcadas pero en un solo fichero. Funciona y está bien organizado, pero es candidato a partirse más (p. ej. Table Sync a su propio `.c`), con el mismo criterio que ya aplicaron a `ui_mp.c` y `settings.c`. |
| S3 | SRP (menor) — sin `extern void` sueltos | 🟡 | Quedan 4: `reset_all_values` (definida en `knob.c`, sin declarar en ningún header) usada así en `game_mode.c`, `mp_victory.c`, `nav.c`; y `start_player_selection_animation` en `intro.c` (ya declarada en `game_state.h`, el `extern` es redundante). Mismo smell que arreglé en mi Fase 2 sobre `main` — aquí se coló en 4 sitios. |
| DRY | La secuencia log+clamp+acción-deshacer+check+sync se repite | 🟡 | `apply_life_delta`, `damage_apply`, `apply_attack_cmd_damage`, `apply_attack_poison` y `apply_counter_edit` repiten las mismas 4-5 líneas. No es una violación de capas, es una oportunidad de no-repetirse. |
| Encapsulación | Estado compartido como `extern` sueltos vs. accesores | 🟡 | `game_state.h` expone `extern int player_life[MAX_DISPLAY_PLAYERS]` etc. directamente (25 símbolos), a diferencia de `prefs_table.h`, que sí usa accesores (`prefs_get_life_total()`). Es una inconsistencia de estilo interna, no un problema de capas: todo sigue dentro del dominio. |
| L | LSP | N/A | Sin jerarquías de tipos; no aplica, igual que en la revisión anterior. |

## Lo que NO hace falta hacer

- Repetir la extracción `core/life.c`/`core/elimination.c`/puerto de
  eventos/puerto de config: **ya existen** como `game_types.h`,
  `game_hooks.h/.c`, `prefs_table.h/.c`. Mis ramas locales
  `refactor/fase-0/1/2` (hechas sobre `main`) quedan obsoletas frente a
  esto; las dejo intactas por si acaso, pero no se van a fusionar aquí.
- Reescribir el registro de pantallas de `knob.c`: ya es una tabla.
- Partir `settings.c`/`ui_mp.c`: ya está hecho.

## Plan de trabajo (corto, 3 puntos, cada uno su propia rama/commit)

### Punto 1 — Quitar los 4 `extern void` sueltos que quedan ✅ Cerrado

Rama `arch/remove-blind-externs`, commit `e949e07`.
Mismo fix que ya apliqué y verifiqué sobre `main`: incluir el header
real en vez de redeclarar a ciegas.
- `start_player_selection_animation`: `intro.c` ya puede incluir
  `game_state.h` (o `game.h`, que lo reexporta) — la declaración ya
  existe ahí.
- `reset_all_values`: no está declarada en ningún header. Se añade a
  `knob.h` (donde vive su definición) y `game_mode.c`/`mp_victory.c`/
  `nav.c` lo incluyen en vez de declararlo sueltos.
- **Cierre:** `grep -rn "^extern void" knobby/src/*.c knobby/*.c` → 0
  líneas. `make test`, `make firmware` en verde.

### Punto 2 — Unificar la secuencia repetida de "aplicar un cambio" ✅ Cerrado

Rama `arch/remove-blind-externs`, commit `66c1151`.
Extraer un helper interno a `game_state.c` (no hace falta exponerlo en
el header, es un detalle de implementación) que encapsule
log+clamp+acción-de-eliminación+check+sync, y hacer que
`apply_life_delta`/`damage_apply`/`apply_attack_cmd_damage`/
`apply_attack_poison`/`apply_counter_edit` lo llamen. Sin cambiar
ninguna firma pública.
- **Cierre:** mismo comportamiento (`make test` sin tocar los tests
  existentes), menos líneas duplicadas.

### Punto 3 (opcional, preguntar antes) — Partir Table Sync de `game_state.c`
Mover `net_sync_fill_state`/`net_sync_apply_state`/
`net_sync_fill_names`/`net_sync_apply_names`/`net_sync_commit_names`/
`net_sync_begin_game`/`net_sync_reset_versions` a un `game_state_sync.c`
propio (declaradas ya en `net_sync.h`, no cambia el header público).
Reduce `game_state.c` en ~200 líneas. Es puro movimiento de código, cero
riesgo, pero lo marco opcional porque el fichero actual ya es legible
por secciones y esto es preferencia de organización, no una corrección.

El `esp_random()` directo (D3) y los accesores de `player_life[]` etc.
(Encapsulación) los dejo fuera del plan salvo que me digas que los
quieres: son mejoras reales pero marginales, y no quiero inflar el
alcance solo por completismo.

## Aviso sobre la revisión anterior

Mi primera revisión de esta conversación, y las Fases 0/1/2 que llegué
a commitear (ramas `refactor/fase-0-red-de-seguridad`,
`refactor/fase-1-core-life-elimination`,
`refactor/fase-2-puerto-de-eventos`), se hicieron contra `main`, sin
comprobar antes si existían otras ramas con trabajo más reciente. Esa
comprobación (`git branch -a` / `git fetch`) debí haberla hecho antes
de la primera revisión, no después de que notaras la regresión en el
dispositivo. Las ramas y el plan viejo se quedan como están, sin
mezclarse con este; si en algún momento quieres que las borre, dímelo.
