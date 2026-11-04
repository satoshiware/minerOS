# Starminer split firmware

Status: This plan is `docs/runbooks/starminer-split-firmware.md` on branch `split` of [baveet256/StarMiner](https://github.com/baveet256/StarMiner). `origin` is that fork. `split` started from `3bf9c3e` (8 Jul 2026). Phase 3 passed on 2026-11-04. The miner at `http://192.168.38.62` is still running `split-p3` with Pool mode Fallback until `split-p4` is installed. `build/` is the uninstalled `split-p4` image. The booted `split-p3` copies are `../phase3/esp-miner.bin` and `../phase3/www.bin`. The `split-p2` copies are `../phase2/`. `V1.0.1-rc.5` is not on disk.

This runbook is the whole handoff. A new session should follow it and should not reconstruct the design from chat history.

This runbook adds split mining to StarMiner. The bench proves it on the Starminer at `192.168.38.62`. Open that miner in a browser on the same LAN and use AxeOS. There is no USB cable between the computer and the miner. Split is a decoupled addition: with the switch off, the stock pool path is the code that runs, and the ASIC driver is not modified.

## Decisions already made

- The bench miner is the Starminer at `192.168.38.62`. Every test is done from its AxeOS pages over Wi-Fi. The computer never installs firmware over USB.
- Do not install a bitaxeorg image on this miner, including `phase0/esp-miner-v2.15.4.bin`. Starminer’s own page says that bricks this device.
- Stock StarMiner AxeOS has no split. The work is on the fork [baveet256/StarMiner](https://github.com/baveet256/StarMiner). AxeOS is the Angular app at `main/http_server/axe-os`. The build uses ESP-IDF v5.5.3, which is what this tree’s CI pins. The device reports v5.5.2.
- The **Pool** page on this tree has two forms: **Pool Configuration** and **Fallback Pool Configuration**. It does not have eight pool cards. Pool 1 is the primary form. Pool 2 is the fallback form. Pool 3 is a third form this work adds. There is no pool 4 through 8.
- The exchange website does not push settings to the miner.
- This checkout is the firmware we change. Split sits on top of the stock pool path. It does not replace it.
- The Phase 0 baseline host is `mining.btcofaz.com` port `3336`. That host is the reverse proxy. The proxy points at the translator, and the translator points at the pool. Protocol on the bench is Stratum V1. The pass/fail reading is that worker’s row on the node. Those values are the bench test. They are not built into the firmware. A slot keeps the protocol already stored on that form, V1 or V2.
- The Pool 1 user on the miner for the Phase 1 run is `slims.bhora.axeminer`. The fallback user `slims.bhora.bitaxe1` is stored and was not used.
- Suggested difficulty stays at `1000`, the miner default. Vardiff is enabled toward the translator, so the translator and the pool set the working difficulty.
- The Phase 0 baseline reading is Pool 1 alone. The fallback form stayed filled during Phase 1 and received no work.
- Pool 2 for the split is `mining.btcofaz.com:3336`, user `sc2.baveet.miner_split`, Stratum V1. The password is typed on the miner and is not written here. Pool 3 is written before Phase 5.
- Frequency and voltage stay at this miner’s defaults: 525 MHz and 1150 mV.
- The stratum password is typed on the miner only. Do not copy it into git, this file, or the record sheet.
- Split is even across filled, live forms among Pool 1, Pool 2, and Pool 3. There is no percent field.
- The **Split** switch on the AxeOS **Pool** page is what makes those forms active. Off, stock fallback behavior stays in force.
- Tests use the path already on the miner: reverse proxy, then translator, then pool. Do not add another hop. Do not change frequency, voltage, or suggested difficulty to make a phase pass.
- The device board string before the update was `601a-1`. The board table maps `601a-1` to the same GammaV2 config as `601a`: one BM1370, 12 V. After `split-p1` the API reports board `601a`.
- The miner is still running `split-p3` with Pool mode Fallback until `split-p4` is installed. `V1.0.1-rc.5` is not on disk, and it is not the Git tag `v1.1.5` (`04e8c22`). An update from this computer cannot put `V1.0.1-rc.5` back. `build/` is `split-p4`: `esp-miner.bin` SHA-256 `afe7d120344e9a3bfd071860df7da939cb548384dae9a2e3a4aaebef8326498d`, `www.bin` SHA-256 `3f8c40085a3a28bdeab5a372c4d3acc1eca4fe5364a133f622d3a907343a6973`. The booted `split-p3` copies are `../phase3/esp-miner.bin` SHA-256 `c098387f749fcd64b4a7cc8552257e2bf243e48e48d67a66bebc1d57123eafce` and `../phase3/www.bin` SHA-256 `00f0cb6abd470a7130df824a2a23f2d9a4d78a8ddac7e28c1608ea47104dd368`. The `split-p2` copies are `../phase2/esp-miner.bin` SHA-256 `e8ccc0bdf15cd7ad307e3bb3dd5978b091cdfe78a94ab85a97ff02d47235ef6d` and `../phase2/www.bin` SHA-256 `9b460f8bc189538f6853118a2e0a70b2b826da79fd6ca62359b3c79ab2d29763`.

Read these existing files before editing the fork. The slice timer is `main/tasks/create_jobs_task.c`. Pool values are stored from `main/nvs_config.c`. The stock connection is `main/tasks/protocol_coordinator.c`, with `main/tasks/stratum_v1_task.c` and `main/tasks/stratum_v2_task.c`. The page to change is `main/http_server/axe-os`. When Split is on, a share has to be submitted on the session that owned the slice. When Split is off, the stock submit path stays.

Stock StarMiner AxeOS does not do this. It stores one primary pool and one fallback pool. The fallback is used after the primary fails to connect three times. The miner hashes one pool at a time. One job is sent to the chain. GammaV2 has one chip, so that one job is the whole split on this bench.

The ASIC drivers stay as they are. Split does not address chips one by one.

## What “split” means

The bench is one BM1370 on a 12 V GammaV2. A Bitaxe Gamma 601 or 602 is the same shape: one chip, roughly 1.0 to 1.8 TH/s. Chip count does not pick a different split. This tree also has 1-chip, 2-chip, and 6-chip families. This test installs only on the Starminer at `192.168.38.62`.

| Filled pools | What the chain does |
| --- | --- |
| 2 | Jobs alternate. Each pool gets half of them. |
| 3 | Jobs rotate. Each pool gets one third of them. |

The chip hashes the current job. At one instant the board is on one pool. Over a minute every filled pool has received its share of the jobs, and each share goes to the pool that owned that job.

## Done

The work is done when all of these are true:

- The dashboard version is `split-p6`.
- **Pool** still has Stratum Host, Stratum Port, Stratum Protocol, User, and Password.
- A **Split** switch on that page leaves the pool boxes visible and chooses which ones are active.
- With the switch off, the miner follows stock AxeOS: one primary, and the fallback only after the primary fails.
- With the switch on, Pool 1 and Pool 2 filled, and Pool 3 empty, the miner holds the two-pool job share for one hour.
- **Split**, with all three filled, holds the three-pool job share for one hour.
- A slot left empty receives no shares, and its share of the work moves to the filled slots.
- A filled pool that cannot log in releases its share until it can log in again.
- A StarMiner image that has booted on this GammaV2 can be installed again from the AxeOS **Update** page. `V1.0.1-rc.5` cannot. It is not on disk.

## Pool page contract

The page keeps the two forms it has now, and this work adds a third form for Pool 3. Host and port stay separate boxes. The bench uses **Stratum V1**. TLS and Stratum V2 stay on each form. A pasted `stratum+tcp://host:port` is split into the host and port boxes, and the same happens for `stratum+tls://` and `stratum+ssl://`.

| Field | Bench value |
| --- | --- |
| Pool 1 host | `mining.btcofaz.com` |
| Pool 1 port | `3336` |
| Pool 1 user | `slims.bhora.bitaxe` |
| Pool 2 | `mining.btcofaz.com:3336`, user `sc2.baveet.miner_split`, Stratum V1. Password is typed on the miner. |
| Pool 3 | A third host, port, and user written on the record sheet before Phase 5 |
| Password | The stratum password for that pool. It is typed on the miner and is not copied into this repo. |

Use another worker suffix if `bitaxe` is already taken. Write the final names on the record sheet and use those names afterward. The same host and port in more than one slot is valid. The user is what separates the accounts.

**Pool mode** is a switch at the top of the **Pool** page. The pool boxes under it stay on the page either way. The switch decides which boxes the miner is obeying.

- **Off, labeled Fallback.** Stock StarMiner. The primary form is mined. The fallback form is used after the primary fails to connect three times. Pool 3 is stored and is not mined. The split panel is visible and inactive.
- **On, labeled Split.** Pool 1, Pool 2, and Pool 3 are the members. The primary and fallback forms stay on the page and stop choosing the job until the switch is turned off.

A slot is filled when host, port, and user are all set. Turning Split on with only Pool 1 filled mines 100% on Pool 1. Turning it on with Pool 1 and Pool 2 filled shares evenly between those two. Pool 3 joins only when it is filled too.

Helper text while the switch is on:

```text
Split is on. Filled pools share the work evenly. An empty pool is unused.
```

Helper text while the switch is off:

```text
Split is off. The miner uses the primary pool, and the fallback pool only if the primary fails.
```

**Save**, then **Restart**, applies the switch and the pool boxes together.

## How the work is divided

The share is even across the filled pools that currently have a job. Empty forms are left out. A filled pool with no job is left out until it has one.

| Filled and live | Pool 1 | Pool 2 | Pool 3 |
| --- | --- | --- | --- |
| Pool 1 | 100% | — | — |
| Pool 1 and Pool 2 | 50% | 50% | — |
| All three | 33% | 33% | 33% |
| Pool 2 down, others up | 50% | 0% until it returns | 50% |

There is no weight field. Three filled pools means one third each, not a custom ratio.

The miner keeps one Stratum session open per filled slot while Split is on. Each session uses the protocol stored on that slot and holds that pool’s latest template. A template stays in use until the pool replaces it. The firmware cuts a short slice from a template, with a new extranonce, on the ASIC job timer AxeOS already uses. It sends that one slice through the existing job send, so every chip on the board hashes it. There is no backlog of slices waiting in a queue, so a new block is not stuck behind old work. The chips keep hashing the current slice while the next one is built. They wait only when every filled pool is down.

The divider runs once per slice:

1. Count how many slices each live pool has already been given in this split.
2. Cut the next slice from the pool that is furthest below its even share.
3. Stamp that slice with the pool slot and send it to the chain.
4. When a nonce comes back, submit it only to the stamped pool, with that pool’s user and password.

Two live pools therefore take turns one slice at a time: Pool 1, Pool 2, Pool 1, Pool 2. Three live pools go Pool 1, Pool 2, Pool 3, and then repeat. After a minute the counts sit on the percentages above because the slices were handed out evenly. A pool that drops out is removed from the count, and the next slice comes from a pool that still has a template. When the missing pool has a template again, it re-enters on the following tick and the even split returns within 60 seconds.

That sequence is the whole split on a one-chip board and on a two-chip or six-chip board. The chips hash one slice at a time, together. Both pools still receive shares in the same minute because the slices alternate on the job timer.

The dashboard keeps the existing hashrate card and adds one line per slot:

```text
Pool 1 · slims.bhora.bitaxe · 50% of jobs · <hashrate>
Pool 2 · <worker> · 50% of jobs · <hashrate>
Pool 3 · empty
```

When a filled pool has no job, its share moves to the filled pools that do. The dashboard shows that pool as down. When its job returns, the even split returns within 60 seconds. If every filled pool is down, hashing pauses and the miner keeps retrying those addresses.

## How a split phase passes

Start the clock 10 minutes after the restart that applied the page.

Every split phase has to pass the job test:

1. Over a 60-second sample in the middle of the run, the job log shows each filled pool getting its even share of jobs, within ±10 percentage points.
2. A share is submitted only to the pool stamped on that job.
3. Repeat the paired reading at the start, middle, and end of the window.
4. Each live pool’s node hashrate stays inside the band below. An empty pool accepts no shares.
5. Rejected shares stay under 2% of submitted shares on each live pool.
6. Frequency and voltage are the values recorded in Phase 0.

| Filled pools | Each pool’s share of the Phase 0 node hashrate | Window |
| --- | --- | --- |
| 1 | 85% to 115% | 30 minutes |
| 2 | 40% to 60% | 60 minutes |
| 3 | 25% to 42% | 60 minutes |

Suggested difficulty stays at `1000`. Vardiff on the translator sets the working difficulty, and the pool handles it from there. The job-log test is still required.

On a multi-chip board, every share from one slice carries that slice’s pool. Chips moving from pool to pool together, as the slices change, is the expected result.

## Bench rules

- The only miner in this test is the bhora Starminer 601 at `http://192.168.38.62`.
- Do not install a bitaxeorg image, including `phase0/esp-miner-v2.15.4.bin` and the factory `601` file. Starminer says that bricks this device.
- The last StarMiner image that booted on this GammaV2 is the rollback file, once one exists. `V1.0.1-rc.5` is not on disk. Install from the AxeOS **Update** page.
- Frequency and voltage stay at the miner defaults recorded in Phase 0: 525 MHz and 1150 mV. Fan settings stay as they are. An AxeOS firmware update leaves those settings on the miner.
- The miner connects to the reverse proxy on the page, `mining.btcofaz.com:3336`. That proxy points at the translator, and the translator points at the pool. Leave that path as it is.
- Pool passwords stay on the miner.
- One test image is installed at a time. The dashboard version has to match the phase.

## Tools

| Tool | Use |
| --- | --- |
| The bhora Bitaxe | The only miner these images are installed on. The image is the same addition for any board this firmware supports. |
| Browser on the same LAN | AxeOS, including **Pool** and **Update** |
| Computer with Docker, or ESP-IDF and Node | Builds `build/esp-miner.bin`. The miner does not compile it. |
| The node page for each worker | Accepted hashrate |

Build from this repo, `https://github.com/baveet256/StarMiner.git`, branch `split`. The checkout does not include submodules yet. The repo’s own path is Docker (`docker build -t espminer-build .devcontainer`, then `idf.py build`) or a local ESP-IDF v5.5.3 install, then:

```bash
idf.py build
```

The install file is `build/esp-miner.bin`.

## Install and recovery

1. Open the bhora miner in a browser on the same LAN. On `split-p1`, hard-refresh the page if it still shows the old Starminer notice. That notice was the `V1.0.1-rc.5` page cached by the browser.
2. Open **Update**.
3. Under **Update Firmware**, choose the phase `build/esp-miner.bin` from this StarMiner tree. Do not choose a bitaxeorg file. The `V1.0.1-rc.5` page had those buttons disabled, so the Phase 1 install was sent from this computer:

```bash
curl -X POST -H "Content-Type: application/octet-stream" --data-binary "@build/esp-miner.bin" http://192.168.38.62/api/system/OTA
curl -X POST -H "Content-Type: application/octet-stream" --data-binary "@build/www.bin" http://192.168.38.62/api/system/OTAWWW
```

4. Wait until the miner reboots. Upload `build/www.bin` in the same way when the web version must match. A web upload done after boot does not change the AxeOS version line until the next restart.
5. Open AxeOS again. Wi-Fi, pools, frequency, and voltage stay as they were. Open **Pool**.

If a test image does not bring AxeOS back, stop. Do not install a second custom image. If AxeOS is up, reinstall the `split-p2` copies at `../phase2/esp-miner.bin` and `../phase2/www.bin`. `build/` is the image being tested and is overwritten by the next build. This test has no USB step. `V1.0.1-rc.5` cannot be restored from this computer.

## Record sheet

```text
Starminer, http://192.168.38.62
Model: GammaV2
Board version: 601a-1
ASIC: BM1370
Chip count: 1
Class hashrate: a single-chip Gamma 601 or 602 is roughly 1.0 to 1.8 TH/s.
Firmware: split-p3
AxeOS: split-p3
ESP-IDF: v5.5.2
Previous firmware: V1.0.1-rc.5, not on disk
Rollback firmware: build/esp-miner.bin
Rollback firmware SHA-256: c2d755419bea5bb8dcef738ff4ff44f3d41791d1d6e2511fadba6fd086c1d204
Rollback web file: build/www.bin
Rollback web SHA-256: e08059018f5fcaeb4fd2c33efc4392548ee71c9396c639479a344bb5efc1491d
MAC: 3C:DC:75:FD:C8:54
Wi-Fi SSID: mullets
Wi-Fi RSSI: -40 dBm
Uptime when read: 1 day, 23 hours, 46 minutes, 36 seconds
Reset reason: Software reset due to exception/panic
Image that belongs here: Starminer firmware for this board. Not bitaxeorg.

Frequency: 525 MHz (miner default)
Voltage: 1150 mV (miner default)
Suggested difficulty: 1000 (miner default; vardiff is on toward the translator)
Path: miner → reverse proxy mining.btcofaz.com:3336 → translator → pool
Phase 0 node hashrate (30 min): 1.062 TH/s
Phase 0 node hashrate (1 day): 1.077 TH/s
Phase 0 dashboard hashrate: about 1.06 TH/s on both the 10-minute and 1-hour averages
One-pool band (85–115% of 1.062): 0.903 to 1.221 TH/s
Two-pool band (40–60% of 1.062): 0.425 to 0.637 TH/s
Three-pool band (25–42% of 1.062): 0.266 to 0.446 TH/s
Pool 1 host: mining.btcofaz.com
Pool 1 port: 3336
Pool 1 user: slims.bhora.axeminer
Fallback host: mining.btcofaz.com
Fallback port: 3336
Fallback user: sc2.baveet.miner_split
Fallback used in Phase 1: no
Pool mode after Phase 3: Fallback. Pool 2 is stored and idle.
Pool 2 host / port / user for the split: mining.btcofaz.com:3336 / sc2.baveet.miner_split / Stratum V1
Pool 3 host / port / user (before Phase 5):
Node page for Pool 1: Pool 1 worker on mining.btcofaz.com
Node page for Pool 2:
Node page for Pool 3:
```

Do not upload `phase0/esp-miner-v2.15.4.bin` or any other bitaxeorg file to the Starminer at `192.168.38.62`.

| Phase | Date | Dashboard version | Result | Notes |
| --- | --- | --- | --- | --- |
| 0 | 2026-11-02 | V1.0.1-rc.5 | readings recorded | Node 1.062 TH/s over 30 min, 1.077 TH/s over a day. AxeOS about 1.06 TH/s at 10 min and 1 hour. Suggested difficulty 1000, vardiff on the translator. Path is reverse proxy, translator, pool. Nothing uploaded. |
| 1 | 2026-11-03 | split-p1 | passed | Node 1.040 TH/s over 30 min. Dashboard about 1.06 / 1.07 / 1.07 TH/s at 1 / 10 / 60 min. 1733 shares accepted, 6 rejected. One BM1370. Fallback stored and unused. Stock Pool fields present. Restart came back as split-p1. Frequency 525 MHz, voltage 1150 mV. |
| 2 | 2026-11-04 | split-p2 | passed | Logs at `/#/logs`. 2763 job lines, all pool 1. 189 share lines, chip 0, pool 1, accepted, same job id. Dashboard 1.072 TH/s over the hour. Node longer window 0.983 TH/s, inside 0.903 to 1.221. Pool unchanged. 601 accepted, 1 rejected. |
| 3 | 2026-11-04 | split-p3 | passed | Split window: Pool 2 connected on the miner, templates arriving, 0 shares. Site showed miner_split offline. axeminer 1.047 / 0.942 / 1.091 TH/s, all inside 0.903 to 1.221. Chip hour 1.073 TH/s. 408 accepted, 1 rejected. Fallback restart: split off, Pool 2 idle, only slims.bhora.axeminer authorized, 202 jobs and 74 shares all pool 1. |
| 4 | | | | |
| 5 | | | | |
| 6 | | | | |

---

## Phase 0 — Identify the Starminer and do not install yet

No split code. No image is uploaded.

The bench AxeOS is `http://192.168.38.62`. It is a Starminer GammaV2, board `601a-1`, one BM1370, firmware `V1.0.1-rc.5`, ESP-IDF v5.5.2.

### Pre-checks

- [x] The Starminer AxeOS opens in a browser on the same LAN.
- [x] Model, board version, ASIC, and firmware version are on the sheet.
- [x] This tree’s GammaV2 key is `601a`, one BM1370, 12 V. The device string `601a-1` is not in the table.
- [x] `V1.0.1-rc.5` is not on disk. Git tag `v1.1.5` is a different commit from master `3bf9c3e`.
- [x] Chip count is 1. A Gamma 601 or 602 is a single chip, roughly 1.0 to 1.8 TH/s, and this tree’s GammaV2 entry is one BM1370.
- [x] Frequency is 525 MHz and voltage is 1150 mV, the miner defaults.
- [x] Pool 1 is `mining.btcofaz.com:3336`, user `slims.bhora.bitaxe`. The fallback form is empty, so the baseline is that pool alone.
- [x] The Pool 1 worker on `mining.btcofaz.com` is the node page. Node hashrate is 1.062 TH/s over 30 minutes and 1.077 TH/s over a day. AxeOS is about 1.06 TH/s on the 10-minute average and on the 1-hour average.
- [x] Suggested difficulty is `1000`. Vardiff is enabled toward the translator. The miner’s host is the reverse proxy, which points at the translator, which points at the pool.

### Steps

1. Do not install `phase0/esp-miner-v2.15.4.bin` or any other bitaxeorg file on this miner.
2. Do not install a StarMiner build until `601a-1` is handled as GammaV2, the same config as `601a`.
3. The baseline is on the sheet. Node 30-minute hashrate is 1.062 TH/s. AxeOS 10-minute and 1-hour averages are about 1.06 TH/s. Leave frequency at 525 MHz, voltage at 1150 mV, and suggested difficulty at 1000.

Fail path: an install that does not bring AxeOS back cannot be undone from this computer. `V1.0.1-rc.5` is not on disk.

---

## Phase 1 — Our build mines one pool

The image is this StarMiner tree plus a version string. Pool behavior is still one pool. This proves the build and the AxeOS update.

### Pre-checks

- [x] Phase 0 readings are on the sheet, and `601a-1` is handled as GammaV2.
- [x] The fork builds with `idf.py build`, and `build/esp-miner.bin` is the install file.
- [x] The built image version is `split-p1`.
- [x] Frequency stayed 525 MHz and voltage stayed 1150 mV. Pool 1 stayed the primary. The fallback form was left as stored, `slims.bhora.bitaxe1`, and was not used.

### Steps

1. Build this StarMiner tree. Board `601a-1` uses the same GammaV2 config as `601a`.
2. The dashboard version comes from `version.txt` and is `split-p1` for both the firmware and AxeOS.
3. `V1.0.1-rc.5` had the Update buttons disabled, so `build/esp-miner.bin` and `build/www.bin` were posted to the miner from this computer. Wi-Fi, pools, frequency, and voltage stayed on the miner.
4. Pool 1 was already `mining.btcofaz.com:3336`, user `slims.bhora.axeminer`. It was left as stored.

### Post-tests

- [x] After the restart, firmware and AxeOS both report `split-p1`.
- [x] **Pool** loads and still offers the stock fields.
- [x] The 30-minute node hashrate is 1.040 TH/s, inside 0.903 to 1.221 TH/s. The dashboard read about 1.06 TH/s at 1 minute and 1.07 TH/s at 10 minutes and 1 hour.
- [x] One BM1370 is up.
- [x] The miner did not connect to the fallback. Fallback user `slims.bhora.bitaxe1` got no work.
- [x] Rejected shares were 6 of 1739 submitted, under 2%.
- [x] A restart booted `split-p1` again without another update. The device reset-reason text is still “Software reset due to exception/panic”, the same text it reported before this install, and it resumed hashing.

Fail path: stop if AxeOS does not come back. If AxeOS is up, reinstall `split-p2` from `../phase2/esp-miner.bin` and `../phase2/www.bin`. `V1.0.1-rc.5` is not on disk.

---

## Phase 2 — Job and share log

Mining stays on Pool 1. The log records every job sent to the ASIC and every share sent to a pool. Later phases are unreadable without it.

### Pre-checks

- [x] Phase 1 post-tests passed.
- [x] `build/esp-miner.bin` and `build/www.bin` are the installed `split-p2` image. `V1.0.1-rc.5` is not on disk.
- [x] The installed image reports `split-p2`. `build/esp-miner.bin` SHA-256 `e8ccc0bdf15cd7ad307e3bb3dd5978b091cdfe78a94ab85a97ff02d47235ef6d`. `build/www.bin` SHA-256 `9b460f8bc189538f6853118a2e0a70b2b826da79fd6ca62359b3c79ab2d29763`.
- [x] The new log lines are readable from AxeOS at `/#/logs`.

### Steps

1. Each job sent to the chain logs one line: `split job time=<us> pool=<slot> job=<id>`. Pool 1 is the primary. Pool 2 is the fallback. One line is one slice.
2. Each share logs one line when the pool answers, or when the submit cannot be written: `split share time=<us> chip=<n> pool=<slot> job=<id> result=<accepted|rejected>`. The chip number is `asic_nr` from the ASIC result. The pool slot is the one stamped on that job.
3. Install `split-p2` from the AxeOS **Update** page, both `build/esp-miner.bin` and `build/www.bin`. Leave Pool 1 as it is.

### Post-tests

- [x] Firmware and AxeOS both report `split-p2`.
- [x] The log sample shows 2763 job lines, and every one says pool 1.
- [x] 189 accepted shares say pool 1 and the same job id as the jobs.
- [x] This bench is one chip. Every share line says chip 0.
- [x] After an hour up, the dashboard is 1.072 TH/s. The node’s longer window for `axeminer` is 0.983 TH/s. Both are inside 0.903 to 1.221 TH/s. The node’s short window, 0.600 TH/s, is the share estimate and sits below the chip counter.
- [x] Pool 1 is still `slims.bhora.axeminer`. The fallback is still stored and unused. Frequency 525 MHz, voltage 1150 mV.

Fail path: stop if AxeOS does not come back. If AxeOS is up, reinstall `split-p2` from `../phase2/esp-miner.bin` and `../phase2/www.bin`. `V1.0.1-rc.5` is not on disk. A missing job id blocks every later phase.

---

## Phase 3 — Second session, all work still on Pool 1

The miner opens Pool 2 and keeps a job from it. Every job sent to the chain is still Pool 1. Pool 2 must show connected and zero shares. **Pool mode** is **Split** for the session setup, with the slice source forced to Pool 1, so this build cannot be mistaken for the finished split.

### Pre-checks

- [x] Phase 2 post-tests passed.
- [x] Image version `split-p3`. Built, not installed. `build/esp-miner.bin` SHA-256 `c098387f749fcd64b4a7cc8552257e2bf243e48e48d67a66bebc1d57123eafce`. `build/www.bin` SHA-256 `00f0cb6abd470a7130df824a2a23f2d9a4d78a8ddac7e28c1608ea47104dd368`.
- [x] Pool 2’s host, port, and user are on the sheet. They are typed as given. Pool 1 stays `mining.btcofaz.com:3336` / `slims.bhora.axeminer`.
- [x] Both node pages are open.
- [x] Pool 3 is empty.
- [x] The last StarMiner image that booted is on disk. `V1.0.1-rc.5` is not.

### Steps

1. Open one Stratum session per filled slot and store the latest job from each. Use the protocol already stored on that slot.
2. Force every job sent to the chain to Pool 1 while Pool 2 is connected. Show that override on the dashboard.
3. Install `split-p3` from the AxeOS **Update** page.
4. Set Pool 1 and Pool 2, mode **Split**, Pool 3 empty. **Save** and **Restart**.

### Post-tests

- [x] Version is `split-p3`.
- [x] Dashboard shows Pool 2 connected and 0% of jobs.
- [x] For 30 minutes, Pool 1 stays inside the one-pool band.
- [x] Pool 2’s node user shows a session and zero accepted shares for that window. The site lists `miner_split` offline because it accepted none.
- [x] The job log shows Pool 2 jobs arriving, and every ASIC job line says Pool 1.
- [x] Switching **Pool mode** to **Fallback** and restarting returns stock behavior: Pool 2 is idle unless Pool 1 fails. New boot had no Pool 2 lines. Only `slims.bhora.axeminer` authorized.

Fail path: stop if AxeOS does not come back. If AxeOS is up, reinstall `split-p2` from `../phase2/esp-miner.bin` and `../phase2/www.bin`. `build/` is the `split-p3` image under test. `V1.0.1-rc.5` is not on disk. A Pool 2 share in the forced-Pool-1 window means the override failed.

---

## Phase 4 — Pool 2 is the second split

Filling Pool 1 and Pool 2, setting **Pool mode** to **Split**, and restarting shares the work evenly. Pool 3 stays empty.

### Pre-checks

- [x] Phase 3 post-tests passed.
- [x] Image version `split-p4` has no force-to-Pool-1 override. Built, not installed. `build/esp-miner.bin` SHA-256 `afe7d120344e9a3bfd071860df7da939cb548384dae9a2e3a4aaebef8326498d`. `build/www.bin` SHA-256 `3f8c40085a3a28bdeab5a372c4d3acc1eca4fe5364a133f622d3a907343a6973`.
- [x] Chip count on the sheet is still the one you will test against.
- [ ] Both node pages are open.
- [ ] The last StarMiner image that booted is on disk. `V1.0.1-rc.5` is not.

### Steps

1. Stamp each job with the pool that supplied it.
2. Give filled pools an even share of jobs.
3. Send each nonce to the pool stamped on its job.
4. Put the Split helper text on the **Pool** page.
5. Install `split-p4` from the AxeOS **Update** page. Set Pool 1 and Pool 2, mode **Split**, Pool 3 empty. **Save** and **Restart**.
6. Warm up for 10 minutes, then measure for 60 minutes.

### Post-tests

- [ ] Version is `split-p4`.
- [ ] Dashboard shows about 50% of jobs on Pool 1 and 50% on Pool 2, Pool 3 empty.
- [ ] The job test in [How a split phase passes](#how-a-split-phase-passes) passes at the start, middle, and end of the hour.
- [ ] Each node hashrate stays inside 40% to 60% of the Phase 0 baseline.
- [ ] Rejected shares stay under 2% on each pool.
- [ ] On a multi-chip board, shares from one slice all name that slice’s pool.
- [ ] Restart again without editing pools. The same split returns after warmup.
- [ ] **Fallback** mode, after a restart, hashes only the primary pool.

Fail path: stop if AxeOS does not come back. If AxeOS is up, reinstall the booted `split-p3` image from `../phase3/esp-miner.bin` and `../phase3/www.bin`. `V1.0.1-rc.5` is not on disk.

---

## Phase 5 — Pool 3 is the third split

Filling all three slots in **Split** mode gives each pool one third of the jobs. The chain still hashes one slice at a time.

### Pre-checks

- [ ] Phase 4 post-tests passed.
- [ ] The sheet has a Pool 3 host, port, user, and node page.
- [ ] Image version `split-p5`.
- [ ] All three node pages are open.
- [ ] The last StarMiner image that booted is on disk. `V1.0.1-rc.5` is not.

### Steps

1. Include Pool 3 in the even job share.
2. Install `split-p5` from the AxeOS **Update** page.
3. Fill all three slots, mode **Split**. **Save** and **Restart**.
4. Warm up for 10 minutes, then measure for 60 minutes.

### Post-tests

- [ ] Version is `split-p5`.
- [ ] Dashboard shows about one third of jobs on each pool.
- [ ] The job test passes at the start, middle, and end of the hour, and all three node users accept shares in those samples.
- [ ] Each node hashrate stays inside 25% to 42% of the Phase 0 baseline.
- [ ] Rejected shares stay under 2% on each pool.
- [ ] On a multi-chip board, shares from one slice all name that slice’s pool.
- [ ] Clear Pool 3, restart, and wait through warmup. The map returns to the Phase 4 two-pool split, and Pool 3 takes no new shares.
- [ ] Fill Pool 3 again, restart, and confirm the three-way split returns.

Fail path: stop if AxeOS does not come back. `V1.0.1-rc.5` is not on disk. If AxeOS is up, `split-p4` can be installed again while this phase is fixed. Leave Pool 3 empty on `split-p4`.

---

## Phase 6 — A dead slot releases its work

The page does not grow. This phase checks drop and return on the Phase 5 code, then tags that image as the one to keep.

### Pre-checks

- [ ] Phase 5 post-tests passed.
- [ ] You can make Pool 2 fail login without changing Pool 1 or Pool 3. A user name that cannot authorize is enough.
- [ ] You can restore the real Pool 2 user afterward.
- [ ] Image version `split-p6`. If `split-p5` already releases a dead pool, the update only changes the version string.
- [ ] The last StarMiner image that booted is on disk. `V1.0.1-rc.5` is not.

### Steps

1. Boot `split-p6` with all three slots filled and confirm the three-way split after warmup.
2. Replace only Pool 2’s user with a name that cannot authorize. **Save** and **Restart**.
3. Watch for 15 minutes.
4. Restore the real Pool 2 user. **Save** and **Restart**.

### Post-tests

- [ ] While Pool 2 cannot log in, the dashboard shows Pool 2 down and 0% of jobs, and Pool 1 and Pool 3 share the work evenly.
- [ ] A 60-second sample in that state shows jobs and shares for Pool 1 and Pool 3 only.
- [ ] After the real Pool 2 user is restored and warmup passes, the three-way split is back.
- [ ] A 60-second sample then shows all three pools.
- [ ] Emptying all three slots and restarting pauses hashing. Restoring Pool 1 only returns the one-pool band over 30 minutes.
- [ ] **Pool mode** **Fallback** still hashes only the primary after a restart.
- [ ] The **Pool** page still has the stock fields plus **Pool mode**.

Fail path: stop if AxeOS does not come back. If AxeOS is up, reinstall `split-p2` from `../phase2/esp-miner.bin` and `../phase2/www.bin`. `V1.0.1-rc.5` is not on disk.

---

## Final sign-off

- [ ] Dashboard version is `split-p6`.
- [ ] The last StarMiner image that booted is still on disk and its hash matches the sheet. `V1.0.1-rc.5` is not on disk.
- [ ] **Split** with Pool 1 and Pool 2 passes the two-pool job test.
- [ ] **Split** with all three pools passes the three-pool job test.
- [ ] An empty slot gets nothing, and a dead slot releases its work.
- [ ] **Fallback** still mines one pool.
- [ ] Frequency and voltage were not changed to make a phase pass.
- [ ] Result rows 0 through 6 are filled in.

That `split-p6` image is the working firmware this runbook is for.
