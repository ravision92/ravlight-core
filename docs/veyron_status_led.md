# Veyron — Legenda Status LED

Riferimento rapido per il pattern mostrato sulla strip (40 px WS2811 + 2 px accent P9813)
quando il modulo **Status LED** è attivo (toggle "Status LED" nel tab Fixture, default ON).

Luminosità di tutti i pattern di stato: **~10%** (26/255) — visibile in sala buia,
ma non invadente durante uno show. La sequenza **Highlight** resta invece a piena
luminosità: è un segnale volontario "guardami qui", non uno stato di sistema.

L'overlay di stato ha **priorità esclusiva sul DMX**: quando attivo, il rendering
DMX normale viene saltato del tutto (non sovrascritto) — un controller che continua
a inviare DMX durante un boot/reconnect/OTA non interferisce con l'indicazione.

| Stato                          | Trigger                                              | Pattern                                                              | Colore   |
|---------------------------------|-------------------------------------------------------|-----------------------------------------------------------------------|----------|
| **Connessione in corso**        | Tentativo di link WiFi/Ethernet (boot o riconnessione) | Due comet che scivolano in un unico verso, dal pixel 1→20 e dal 40→21, poi ripartono da capo (loop, ~1.8 s per giro) | Ambra    |
| **AP fallback attivo**          | Nessuna rete disponibile → SoftAP di configurazione     | Solo primo e ultimo pixel della strip che respirano (pattern sobrio)   | Magenta  |
| **Connesso**                    | IP appena ottenuto (WiFi o Ethernet)                    | Flash pieno ~1.5 s, poi torna automaticamente al DMX normale           | Verde    |
| **Aggiornamento firmware (OTA)**| Upload firmware in corso via `/api/ota/upload`          | Barra di progresso (n° pixel accesi = % completamento) + respiro accent | Blu      |
| **Highlight / Locate**          | Pulsante "Highlight / Locate" o `POST /highlight`       | Comet bianca con coda sfumata (6 px), rimbalza avanti/indietro, 6 s totali; accent respira blu in sincrono | Bianco (accent blu) |

## Note implementative

- Toggle: `veyronConfig.statusLedEnable` (JSON fixture: `"statusLed"`), default `true`.
- Interfaccia condivisa (`include/fixture_config.h`): `fixtureSetNetStatus()` /
  `fixtureSetOtaProgress()` — no-op su Axon/Elyon/Orion, implementate solo su Veyron.
- Chiamate core: `network_manager.cpp` (eventi WiFi/ETH) e `webserver_manager.cpp`
  (handler upload OTA, stessi punti di chiamata di `oledShowOtaProgress()`/`oledOtaEnd()`
  già usati per Axon).
- Stato "Connesso" si auto-azzera dopo 1.5 s (`NET_CONNECTED_FLASH_MS`) e torna al
  DMX normale da solo — non serve nessun evento esterno di "fine".
- `fixtureTickStatus()`: `initEthernet()`/`initWiFi()` bloccano `setup()` in attesa
  del link, prima che `loop()`/`handleDMX()` partano — senza questo hook la comet
  "Connessione in corso" non veniva mai disegnata durante l'attesa del boot.
  Chiamata una volta per iterazione nei due wait-loop di `network_manager.cpp`.
