![alt text](https://raw.githubusercontent.com/AlexPetrusca/Mephisto/master/res/mephisto_banner_lowercase.png)

Mephisto is a browser extension that provides next-best-move analysis and can automate gameplay on Chess.com and Lichess.

<p align="center">
  <img src="https://github.com/AlexPetrusca/Mephisto/blob/master/res/puzzle_rush_demo.gif" align="center" height="500px" />
  <p align="center">Mephisto crushing some Puzzle Rush in real-time</p>
</p>

## Desktop version (C++ / Windows)

Run `build.bat` and choose Chrome/Edge or Firefox/Zen. Both builds produce a self-contained WinUI 3 application ZIP. Chrome also produces an unpacked `extension` directory; Firefox produces an unsigned XPI. Outputs are in `build/chrome` or `build/firefox`. Extract the full application ZIP: WinUI runtime DLLs/resources are required beside the EXE. Disable the old extension and refresh the chess page. Python is no longer required. F10 toggles autoplay; coordinates are verified automatically.

For Firefox 140+, load `build/firefox/Mephisto-Firefox-addon.xpi` temporarily through `about:debugging`; permanent installation needs Mozilla signing. See [Firefox instructions](native/FIREFOX.md). The optional variation mode shows three candidate arrows, chooses among close alternatives and varies timing; failed input has up to three guarded attempts. This does not provide an anti-cheat bypass guarantee.

See [desktop instructions](native/QUICKSTART.md) for setup, variants and verification limits. Build with `powershell -ExecutionPolicy Bypass -File native/build.ps1` (Visual Studio C++ tools required). Validation: `node native/test-bridge.mjs`, `node native/test-collector.mjs`, `python native/test-desktop.py`. The Python file is a test harness only, not a runtime backend.

## Legacy version

The instructions below describe the previous popup-based implementation, retained in source for reference.

## Getting Started

<a href="https://chrome.google.com/webstore/detail/mephisto-chess-extension/ihpdlpgcjepplokoncjelcbbcedgnanp" style="border: 1px solid white">
  <img src="https://github.com/AlexPetrusca/Mephisto/blob/master/res/chrome-web-store.png" align="center" height="66px" />
</a>
&nbsp;&nbsp;&nbsp;&nbsp;
<a href="https://addons.mozilla.org/en-US/firefox/addon/mephisto-chess-extension">
  <img src="https://github.com/AlexPetrusca/Mephisto/blob/master/res/firefox_web_store.png" align="center" height="66px" />
</a>
<br>
<br>

Click the Mephisto icon to open its popup window. It will automatically detect and analyze the current chess position on the page.

For quick access, pin Mephisto to Chrome’s toolbar:
1. Click the puzzle icon next to Chrome’s address bar.
2. Find "Mephisto Chess Extension" and click the pin icon next to it.

Note: The popup window will close if you click outside of it. To keep it open, right-click the pinned Mephisto icon and select "Inspect Popup."

For more details, visit the [Getting Started](https://github.com/AlexPetrusca/Mephisto/wiki/Getting-Started) guide.

## How to Develop Locally
Set up a local install:
1. Clone the repo
2. Navigate to `chrome://extensions` through the Chrome address bar
3. Enable developer mode
4. Click on "Load unpacked" and select the cloned repo folder
5. Mephisto Chess Extension is now installed

Test a code change:
1. Navigate to `chrome://extensions`
2. Reload Mephisto Chess Extension
3. Reload the webpage you want to test on
4. Test the changes

For technical details, see [Technical Overview](https://github.com/AlexPetrusca/Mephisto/wiki/Technical-Overview).


## How to Contribute
Thank you for your interest in contributing to Mephisto! There are many ways to contribute, and we appreciate all of them.

Ways to Contribute:
- Help contribute ideas to Mephisto
- Help identify and document bugs with Mephisto
- Implement requested features through PRs
- Fix identified bugs through PRs
