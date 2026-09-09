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
    const pathname = new URL(request.url, 'http://localhost').pathname;
    const relative = pathname === '/scuzz/' ? 'index.html' : pathname.replace(/^\/scuzz\//, '');
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
    await page.context().grantPermissions(['clipboard-read', 'clipboard-write']);
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const snapshot = () => page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []));
    const expectText = text => page.waitForFunction(text => Module.ccall('sz_web_snapshot', 'string', [], []).includes(text) && Module.textBlocks?.some(block => block.text.includes(text.replace(/^text:/, ''))), text);
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
      assert.equal(new URL(page.url()).hash, '#section=' + ['Start', 'Install', 'Language', 'GUI', 'Verify', 'Web'][i]);
    }
    await page.goBack();
    await expectText('text:Verify behavior');
    await page.goForward();
    await expectText('text:Package for the web');
    await page.mouse.click(65, 44);
    await expectText('text:Count: 1');
    await page.mouse.click(355, 288);
    await expectText('text:Count: 0');
    await page.mouse.click(65, 92);
    await expectText('text:Install and run');
    // Select a command with a real pointer drag and use the browser clipboard.
    const command = page.locator('#text-layer span').filter({hasText: 'curl -fsSL'});
    const box = await command.boundingBox();
    await page.mouse.move(box.x + 1, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + 80, box.y + box.height / 2, {steps: 10});
    await page.mouse.up();
    assert((await page.evaluate(() => getSelection().toString())).startsWith('curl'));
    await page.keyboard.press('Control+c');
    assert((await page.evaluate(() => navigator.clipboard.readText())).startsWith('curl'));
    await page.evaluate(() => getSelection().removeAllRanges());
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
    // Reload and direct fragments select the same section below a project path.
    await page.reload();
    await expectText('text:Package for the web');
    await page.evaluate(() => { location.hash = 'section=Install'; });
    await expectText('text:Install and run');
    const copied = await page.evaluate(() => {
      const lines = [...document.querySelectorAll('#text-layer span')];
      const first = lines.find(line => line.textContent.startsWith('curl -fsSL'));
      const block = lines.filter(line => line.block === first.block);
      const range = document.createRange();
      range.setStart(first.firstChild, 0);
      range.setEnd(block.at(-1).firstChild, block.at(-1).textContent.length);
      getSelection().removeAllRanges();
      getSelection().addRange(range);
      return Module.textBlocks[first.block].text;
    });
    await page.keyboard.press('Control+c');
    assert.equal(await page.evaluate(() => navigator.clipboard.readText()), copied);
    await page.evaluate(() => getSelection().removeAllRanges());
    await page.goto(`http://127.0.0.1:${server.address().port}/scuzz/?preview=1#section=Language`);
    await expectText('text:Language');
    assert.equal(new URL(page.url()).search, '?preview=1');
    await page.evaluate(() => { location.hash = 'section=missing'; });
    await page.waitForFunction(() => location.hash === '#section=Language');
    await expectText('text:Language');
    assert.deepEqual(errors, []);
    console.log('web: selection, clipboard, history, deep links, navigation, Signals, keyboard, touch, scroll, resize, and scale passed');
  } finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
