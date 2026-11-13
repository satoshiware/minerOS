# Set up a miner

## 1. Power on

Plug the miner into power. Wait until the small screen is on.

A new miner shows a setup Wi-Fi name. It looks like `Satoshiware_` followed by four characters, for example `Satoshiware_A1B2`. That name is also on the screen, next to a code you can scan to join.

## 2. Join the miner’s setup Wi-Fi

On a phone or computer, open the Wi-Fi list and connect to that `Satoshiware_` network, or scan the code on the screen. It has no password.

The phone should open the Satoshiware Wi-Fi page. Enter the name and password of the network the miner should use, then save. The miner leaves the setup network and joins that Wi-Fi. If the page does not open, go to [http://192.168.4.1](http://192.168.4.1).

## 3. Find the miner’s address

After it joins, the screen shows an address like `192.168.1.50`. You can also find that address in the router’s device list.

The computer you use for the next step must be on that same Wi-Fi. The install does not run while the computer is still joined to the `Satoshiware_` setup network.

## 4. Install the firmware

Use the address from the miner’s screen in place of the example below.

**Windows.** Open Command Prompt and run:

```bat
curl -fsSL -o %TEMP%\SatoshiWare_miner_firmware.bat https://raw.githubusercontent.com/satoshiware/minerOS/main/SatoshiWare_miner_firmware.bat && %TEMP%\SatoshiWare_miner_firmware.bat 192.168.1.50
```

**Mac.** Open Terminal and run:

```bash
curl -fsSL -o /tmp/SatoshiWare_miner_firmware.sh https://raw.githubusercontent.com/satoshiware/minerOS/main/SatoshiWare_miner_firmware.sh && bash /tmp/SatoshiWare_miner_firmware.sh 192.168.1.50
```

The command downloads the firmware and the web page, installs both, and waits while the miner restarts. Leave the window open until it says the install is finished.

## 5. Open minerOS and set the pool

In a browser, open `http://` followed by the miner’s address. Hard-refresh the page.

The page is Satoshiware minerOS. Open the Pool page and enter that miner’s pool host, port, user, and password, then Save.

Split stays off until you turn it on. Turn Split on only after the pool host is filled in, then restart the miner.
