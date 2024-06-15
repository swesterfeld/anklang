// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
// @ts-check

// constants
const verbose = process.argv.indexOf ('--verbose') >= 0;
loginf ("startup", "verbose=" + verbose);
const devtools = process.argv.indexOf ('--devtools') >= 0;
const localhost = "127.0.0.1";

// == imports ==
loginf ("import puppeteer-core");
import puppeteer from "puppeteer-core";
loginf ("import electron");
import { app, BrowserWindow } from "electron";;
loginf ("import get-port");
import getPort from "get-port";;
loginf ("import fs");
import fs from "fs";;

// == loginf with line numbers ==
const stack_line_rex = /(\d+):(\d+)\)?$/;
function loginf (...args)
{
  if (!verbose)
    return;
  let err;
  try { throw new Error(); }
  catch (error) { err = error; }
  const lines = err.stack.split ("\n");
  const file_line = /([^/]+:\d+):\d+/.exec (lines[2])[1];
  return console.log (`${file_line}:`, ...args);
}

// == configure electron ==
/** Setup electron for remote debugging from puppeteer
 * @returns {number} Remote debugging port
 */
async function electron_configure (listenhost)
{
  loginf ("electron_configure");
  process.env["ELECTRON_DISABLE_SECURITY_WARNINGS"] = true;     // otherwise warns "Insecure Content-Security-Policy"
  loginf ("Electronjs app checks");
  if (!app || app.commandLine.getSwitchValue ("remote-debugging-address") ||
      app.commandLine.getSwitchValue ("remote-debugging-port") || app.isReady())
    throw new Error ("invalid Electronjs App handle");
  loginf ("Electronjs version:", app.getVersion());
  const port = await getPort ({ host: listenhost });
  loginf ("getPort", listenhost, port);
  // options for remote debugging
  app.commandLine.appendSwitch ("remote-debugging-address", listenhost);
  app.commandLine.appendSwitch ("remote-debugging-port", `${port}`);
  app.commandLine.appendSwitch ("disable-dev-shm-usage");       // otherwise will not work inside docker
  app.commandLine.appendSwitch ("disable-software-rasterizer"); // otherwise hangs for 9 seconds after new BrowserWindow
  app.disableHardwareAcceleration();     // same as --disable-gpu; otherwise hangs for 9 seconds after new BrowserWindow
  return port;
}

// == puppeteer connect ==
/** Connect puppeteer with control over the electron app
 * @returns puppeteer broser
 */
async function puppeteer_connect (listenhost, port)
{
  loginf ("await app.whenReady");
  await app.whenReady();
  console.assert (BrowserWindow.getAllWindows().length === 0);

  // get webSocketDebuggerUrl from /json/version; https://pptr.dev/api/puppeteer.browser
  loginf ("fetch /json/version");
  const json_response = await fetch (`http://${listenhost}:${port}/json/version`);
  //loginf ("json response:", json_response);
  const response_object = await json_response.json();
  //loginf ("response object:", response_object);
  loginf ("response webSocketDebuggerUrl:", response_object.webSocketDebuggerUrl);
  loginf ("await puppeteer.connect");
  const puppeteer_browser = await puppeteer.connect ({ browserWSEndpoint: response_object.webSocketDebuggerUrl, defaultViewport: null });
  loginf ("browser:", puppeteer_browser);
  // monkey patch puppeteer_browser.newPage which otherwise cannot work
  puppeteer_browser.newPage = puppeteer_newpage.bind (puppeteer_browser);
  return puppeteer_browser;
}

// == puppeteer getpage ==
/** Find the puppeteer page for an electron browser window.
 * @returns puppeteer page
 */
async function puppeteer_getpage (puppeteer_browser, window)
{
  loginf ("await puppeteer_browser.pages");
  const pages = await puppeteer_browser.pages();
  loginf ("pages:", pages);
  const cookie = "PUPPETEER:" + String (new Date()) + ":" + Math.random(); // must be unique
  loginf ("await executeJavaScript");
  const js1 = await window.webContents.executeJavaScript (`window[':puppeteer_getpage.cookie'] = "${cookie}";`);
  loginf ("executeJavaScript:", js1);
  loginf ("await test_page.evaluate");
  const cookies = await Promise.all (pages.map (test_page => test_page.evaluate (`window[':puppeteer_getpage.cookie']`)));
  loginf ("await executeJavaScript");
  const js2 = await window.webContents.executeJavaScript (`delete window[':puppeteer_getpage.cookie'];`);
  loginf ("executeJavaScript:", js2);
  const index = cookies.findIndex (test_cookie => cookie === test_cookie);
  if (index < 0)
    throw new Error (`failed to find electron window in puppeteer_browser.pages()`);
  const page = pages[index];
  loginf ("page:", page);
  return page;
}

// == puppeteer newPage ==
/** Create a new electron window and return the puppeteer Page for it.
 * @returns puppeteer page
 */
async function puppeteer_newpage (settings = {})
{
  const webPreferences = Object.assign ({
    nodeIntegration:                  false,
    sandbox:                          true,
    contextIsolation:                 true,
    defaultEncoding:                  "UTF-8",
  }, settings.webPreferences);
  settings = Object.assign ({ show: true }, settings, { webPreferences });

  loginf ("new BrowserWindow");
  const window = new BrowserWindow (settings);
  const url = "about:blank";
  loginf ("loadURL:", url);
  await window.loadURL (url);
  if (devtools)
    window.toggleDevTools(); // start with DevTools enabled

  return puppeteer_getpage (this, window);
}

// == main ==
async function main (argv)
{
  loginf ("main:", argv.join (" "));

  // setup
  loginf ("await electron_configure");
  const port = await electron_configure (localhost);
  const window_all_closed = new Promise (r => app.on ("window-all-closed", r));
  loginf ("await puppeteer_connect");
  const puppeteer_browser = await puppeteer_connect (localhost, port);

  // https://developer.chrome.com/docs/puppeteer/get-started#save_a_screenshot
  const page = await puppeteer_browser.newPage (); // { show: false }
  await page.setViewport ({ deviceScaleFactor: 1, isMobile: false, width: 1080, height: 1920 });
  await page.goto ('https://example.com', { waitUntil: 'networkidle2', });
  await page.screenshot({ path: 'example.png', fullPage: true });
  if (!devtools)
    await page.close(); // puppeteer_browser.close();

  // shutdown
  loginf ("await window closed");
  await window_all_closed;
  loginf ("app.quit");
  app.quit();
}

// Start main()
main (process.argv);
