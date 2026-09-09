// Check browser input and static assets against the shared session.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

async function main() {
  const directory = path.resolve(process.argv[2]);
  for (const file of ['index.html', 'app.js', 'app.wasm']) {
    assert(fs.existsSync(path.join(directory, file)), `missing web asset: ${file}`);
  }
  const server = http.createServer((request, response) => {
    const relative = request.url === '/scuzz/' ? 'index.html' : request.url.replace(/^\/scuzz\//, '');
    if (!['index.html', 'app.js', 'app.wasm'].includes(relative)) {
      response.writeHead(404).end();
      return;
    }
    response.setHeader('Content-Type', relative.endsWith('.wasm') ? 'application/wasm' : relative.endsWith('.js') ? 'text/javascript' : 'text/html');
    response.end(fs.readFileSync(path.join(directory, relative)));
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  let browser;
  try {
    browser = await chromium.launch({headless: true, args: ['--no-sandbox']});
    const page = await browser.newPage({viewport: {width: 1000, height: 720}, hasTouch: true, deviceScaleFactor: 2});
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const snapshot = () => page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []));
    const expectText = text => page.waitForFunction(text => Module.ccall('sz_web_snapshot', 'string', [], []).includes(text), text);
    await page.goto(`http://127.0.0.1:${server.address().port}/scuzz/`);
    await page.waitForFunction(() => Module.ccall && Module.ccall('sz_web_snapshot', 'string', [], []).includes('text:Scuzz Docs'));
    assert.equal(await page.title(), 'Scuzz Docs');
    assert.equal(await page.evaluate(() => Module.canvas.width), 2000);
    await page.mouse.click(275, 288);
    await expectText('text:Count: 1');
    const headings = ['Scuzz Docs', 'Install and run', 'Language', 'Build a GUI', 'Verify behavior', 'Package for the web'];
    for (let i = 1; i < headings.length; i++) {
      await page.mouse.click(65, 44 + i * 48);
      await expectText(`text:${headings[i]}`);
      assert(!(await snapshot()).includes('button:Add one'));
    }
    await page.mouse.click(65, 44);
    await expectText('text:Count: 1');
    await page.mouse.click(355, 288);
    await expectText('text:Count: 0');
    await page.mouse.click(65, 92);
    await page.keyboard.press('ArrowDown');
    await page.keyboard.press('Enter');
    await expectText('text:Language');
    await page.setViewportSize({width: 390, height: 720});
    await page.waitForFunction(() => Module.canvas.width === 780);
    await page.touchscreen.tap(65, 140);
    await expectText('text:Package for the web');
    await page.mouse.move(300, 500);
    await page.waitForTimeout(100);
    const before = await page.screenshot();
    await page.mouse.wheel(0, 450);
    await page.waitForTimeout(200);
    const after = await page.screenshot();
    assert(!before.equals(after), 'wheel input must scroll the page');
    assert((await snapshot()).includes('choicechip:Start'));
    assert.deepEqual(errors, []);
    console.log('web: navigation, Signals, keyboard, touch, scroll, resize, scale, and project URL passed');
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
