# Hardware test plan

BunkerNet is built and tested in CI by compiling it and by running its logic in a simulator.
Nothing replaces trying it on real modules. This checklist takes about an hour with **3 nodes
and 2 phones**. Tick each item, and send back anything that fails together with the serial log.

**Setup**
1. `./tools/new-psk.sh`, then `pio run -t upload` on three modules (same key on all).
2. Open a serial monitor on one node (`pio device monitor`, 115200).
3. Place the nodes in a line: A, B, C, with B in the middle and A and C out of each other's range
   if you can (different rooms, or about 20 m apart).
4. Keep one phone near A (phone 1) and one near C (phone 2).

## 1. Boot and health
- [ ] Serial shows `Node-XXXX up`, and no `LittleFS mount failed` and no `WARNING` lines.
- [ ] Time from power-on until the Wi-Fi `BunkerNet` appears: ______ s (PBKDF2 and the first
      flash save run at boot; anything over ~5 s is worth reporting).
- [ ] **Nodes** tab: free heap with 0 phones ______ , with 2 phones ______ , with 4 phones ______.
      Pass: stays above ~8 KB. Under 5 KB a node answers "busy" instead of serving pages.

## 2. The mesh
- [ ] Each node lists the others on the **Nodes** tab, and A lists C at 2 hops via B.
- [ ] A chat message from phone 1 appears on phone 2 within about a second.
- [ ] Unplug B. Within ~45 s B disappears from the list, and messages between A and C stop if
      they are out of range. Plug B back in and everything recovers by itself.
- [ ] Reboot a node (pull the power). Within ~30 s the others show it again and its messages
      go through. This exercises the boot counter and replay protection.

## 3. Private messages
- [ ] Phone 1 sends a private message to phone 2: ✓ then ✓✓ within a couple of seconds.
- [ ] **Offline delivery:** close phone 2's browser, send it a message from phone 1: it shows
      ⏳ stored. Reopen the page on phone 2, ideally on a different node: the message arrives
      and the ⏳ becomes ✓✓.
- [ ] **Survives a power cut:** send a message to an offline contact, then power-cycle every
      node that is up. Bring the recipient online. The message should still arrive.
- [ ] **Privacy:** from a third device on the mesh Wi-Fi, run
      `curl -i "http://192.168.4.1/poll?me=<phone 2 id>&name=x&since=0"` (no `tok`). Expect
      `403`. Without the token, nobody else can read or send as that person.

## 4. Everything else
- [ ] **Roaming:** walk with phone 1 from A to C while chatting. The page keeps working and
      no messages are missing or doubled.
- [ ] **Alerts:** raise 🚨 on phone 1. A red banner (with sound and vibration) appears on
      phone 2. *Clear all* removes it on both.
- [ ] **Data board:** set `water = 60%` on phone 1. It appears on phone 2 with the right time.
- [ ] **Cyrillic and emoji:** send an 80-letter Ukrainian message, then a long one with emoji.
      Nothing should be cut mid-character, and the byte counter appears near the limit.
- [ ] **Rate limit:** send 15 short messages in a few seconds. The extra ones are refused and
      chat keeps working normally afterwards.
- [ ] **Name change:** tap your name, rename. Others see the new name within ~15 s.
- [ ] **Relay-only:** flash a fourth module with `pio run -e esp01_relay -t upload`. It
      should appear on the **Nodes** tab, relay messages, and show no Wi-Fi network.

## 4b. Newer features
- [ ] **Rooms:** on both phones tap *+ Room*, type `watch` on each. Messages in the room reach
      only phones that joined it, and the unread badge appears on the chip.
- [ ] **Priority:** send a message with ❗ from phone 1. Phone 2 shows it highlighted and beeps
      or vibrates (it needs a tap on the page first on some phones, for sound).
- [ ] **Shared clock:** let phone 1 (correct clock) use the mesh for a minute first, so the nodes
      know the right time. Then set phone 2's clock a few minutes wrong (turn off automatic time). Within
      ~15 s the page shows the 🕐 hint, and a message it sends carries the correct time.
      Check the **Nodes** tab shows a plausible *mesh time*.
- [ ] **Supply voltage:** the **Nodes** tab shows a voltage for each node (about 3.3 V). Note
      any that read below 3.0 V.
- [ ] **Languages:** switch the page to Deutsch and Українська from the name dialog and check
      that nothing is cut off.

## 5. Phones and the pop-up
- [ ] iPhone: joining the Wi-Fi opens the page automatically. Does it remember your name
      after closing the pop-up? Does it work in Safari at `http://192.168.4.1`?
- [ ] Android: same questions, plus "Sign in to network" and "this network has no internet,
      stay connected?" prompts. Note what your phone asks.
- [ ] Scan the QR codes on `docs/join-sheet-en.pdf`: the first joins the Wi-Fi, the second
      opens the app.

## 6. Range (optional)
Walk away from one node with a phone until it drops, in the open and through walls.
Open air ______ m, one wall ______ m, two walls ______ m.

**802.11b experiment:** build two nodes with `#define MESH_PHY_11B 1` in `config.local.h` (the
same on both), put them where the normal build just barely loses the link, and see if they now
stay connected. Repeat the walk above. Record the distances ______ m, ______ m, ______ m. Keep the
setting only if it helps and nothing else gets worse (page speed, phones that won't join).

**Browser update (2 MB+ boards only):** with `OTA_PASSWORD` set, open
`http://192.168.4.1/update` and upload a `.bin`. Check that the node reboots into the new version
and keeps its stored messages.

## Reporting
For anything that fails, send: the firmware version, the board, what you did, what you saw, and
the serial log from the relevant node. Free heap and the **Nodes** tab values help a lot.
