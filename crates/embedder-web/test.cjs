// Check the browser controls against shared session state.
const { chromium, firefox, webkit } = require('playwright');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

async function check(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: mobile ? {width: 390, height: 720} : {width: 1000, height: 720},
      hasTouch: mobile, isMobile: mobile, deviceScaleFactor: 2});
    await context.addInitScript(() => {
      window.rafRequests = 0;
      const original = window.requestAnimationFrame.bind(window);
      window.requestAnimationFrame = callback => { window.rafRequests += 1; return original(callback); };
    });
    if (browserType === chromium) await context.grantPermissions(['clipboard-read', 'clipboard-write']);
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', msg => {
      if (/Unable to preventDefault inside passive/.test(msg.text())) errors.push(msg.text());
    });
    const expectText = async text => {
      try {
        await page.waitForFunction(text => Module.ready && Module.ccall('sz_web_snapshot', 'string', [], []).includes(text) &&
          Module.textBlocks?.some(block => block.text === text.replace(/^text:/, '')), text);
      } catch (error) {
        console.error({expected: text, url: page.url(), errors,
          state: await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))});
        throw error;
      }
    };
    const expectSnap = async text => {
      try {
        await page.waitForFunction(text => Module.ready && Module.ccall('sz_web_snapshot', 'string', [], []).includes(text), text);
      } catch (error) {
        console.error({expected: text, url: page.url(), errors,
          state: await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))});
        throw error;
      }
    };
    const expectSection = async id => {
      try {
        await page.waitForFunction(id => Module.ready && Module.currentSection === id, id);
      } catch (error) {
        console.error({expected: id, url: page.url(), errors,
          state: await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))});
        throw error;
      }
    };
    const expectEditor = async text => {
      try {
        await page.waitForFunction(text => [...document.querySelectorAll('textarea[aria-label="editor"]')]
          .some(editor => editor.value.includes(text)), text);
      } catch (error) {
        console.error({expected: text, url: page.url(), errors,
          editors: await page.evaluate(() => [...document.querySelectorAll('textarea[aria-label="editor"]')].map(editor => editor.value)),
          state: await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))});
        throw error;
      }
    };
    const reveal = async locator => {
      await locator.evaluate(node => node.focus());
      await page.waitForFunction(el => {
        const box = el.getBoundingClientRect();
        return box.height > 0 && box.bottom > 0 && box.top < innerHeight;
      }, await locator.elementHandle());
    };
    await page.goto(url);
    await expectText('text:Run inc');
    await expectSection('run');
    assert.equal(await page.title(), 'Scuzz');
    {
      const paints = await page.evaluate(() => Module.ccall('sz_web_paints', 'number', [], []));
      const pumps = await page.evaluate(() => Module.ccall('sz_web_pumps', 'number', [], []));
      const frames = await page.evaluate(() => window.rafRequests);
      assert(paints >= 1, 'first paint');
      await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 400)));
      assert.equal(await page.evaluate(() => Module.ccall('sz_web_paints', 'number', [], [])), paints);
      assert.equal(await page.evaluate(() => Module.ccall('sz_web_pumps', 'number', [], [])), pumps);
      assert.equal(await page.evaluate(() => window.rafRequests), frames, 'idle frame loop');
    }
    assert.equal(await page.getByRole('heading', {name: 'Run', level: 1}).count(), 1);
    assert.equal(await page.getByRole('navigation', {name: 'Breadcrumb'}).count(), 0);
    assert.equal(await page.getByRole('region', {name: 'App bar'}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'Run', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'View', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'Cover', exact: true}).count(), 1);
    assert.equal(await page.getByRole('button', {name: 'Add one', exact: true}).count(), 0);
    assert.equal(await page.getByRole('img').count(), 0);
    const run = page.getByRole('button', {name: 'Run', exact: true});
    await reveal(run);
    await run.click();
    await expectText('text:1');
    assert.equal(await page.getByRole('button', {name: 'Continue', exact: true}).count(), 1);
    const continueRun = page.getByRole('button', {name: 'Continue', exact: true});
    await reveal(continueRun);
    await continueRun.click();
    await expectSection('view');
    await expectText('text:Show a View');
    const viewRun = page.getByRole('button', {name: 'Run', exact: true});
    await reveal(viewRun);
    await viewRun.click();
    await expectText('text:Clicks: 0');
    const plusOne = page.getByRole('button', {name: '+1', exact: true});
    await reveal(plusOne);
    await plusOne.click();
    await expectText('text:Clicks: 0');
    await page.getByRole('tab', {name: 'Check', exact: true}).click();
    await expectSection('check');
    assert.equal(await page.getByRole('tab', {name: 'Main.scuzz', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'count.scuzz_verify', exact: true}).count(), 1);
    await page.getByRole('tab', {name: 'count.scuzz_verify', exact: true}).click();
    await expectEditor('oracle incAdds');
    const check = page.getByRole('button', {name: 'Check', exact: true});
    await reveal(check);
    await check.click();
    await expectText('text:true');
    const signalTab = page.getByRole('tab', {name: 'Signal', exact: true});
    await reveal(signalTab);
    await signalTab.click();
    await expectSection('signal');
    assert.equal(new URL(page.url()).hash, '#stage=signal');
    const addOne = page.getByRole('button', {name: 'Add one', exact: true});
    await addOne.waitFor({state: 'attached'});
    await reveal(addOne);
    await addOne.click();
    await expectText('text:Count: 1');
    {
      const paints = await page.evaluate(() => Module.ccall('sz_web_paints', 'number', [], []));
      const pumps = await page.evaluate(() => Module.ccall('sz_web_pumps', 'number', [], []));
      const frames = await page.evaluate(() => window.rafRequests);
      await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 400)));
      assert.equal(await page.evaluate(() => Module.ccall('sz_web_paints', 'number', [], [])), paints);
      assert.equal(await page.evaluate(() => Module.ccall('sz_web_pumps', 'number', [], [])), pumps);
      assert.equal(await page.evaluate(() => window.rafRequests), frames, 'idle frame loop');
    }
    await page.getByRole('tab', {name: 'Search', exact: true}).click();
    await expectSection('search');
    await expectText('text:Campaign');
    await expectEditor('oracle hidden');
    const fuzz = page.getByRole('button', {name: 'Fuzz', exact: true});
    await reveal(fuzz);
    await fuzz.click();
    await expectText('text:fail hidden 3');
    await expectSnap('chip:fail=1');
    await page.getByRole('tab', {name: 'Signal', exact: true}).click();
    await expectText('text:Count: 1');
    const signalRun = page.getByRole('button', {name: 'Run', exact: true});
    await reveal(signalRun);
    await signalRun.click();
    await expectText('text:Clicks: 0');
    const tryEditor = page.getByRole('textbox', {name: 'editor', exact: true});
    const trySource = await tryEditor.inputValue();
    assert(trySource.includes('Clicks: $n'));
    await tryEditor.focus(); await page.keyboard.press('ControlOrMeta+a');
    await page.keyboard.insertText(trySource.replace('Clicks: $n', 'Taps: $n'));
    await reveal(signalRun);
    await signalRun.click();
    await expectText('text:Taps: 0');
    const plusMounted = page.getByRole('button', {name: '+1', exact: true});
    await reveal(plusMounted);
    await plusMounted.click();
    await expectText('text:Taps: 1');
    await tryEditor.focus(); await page.keyboard.press('ControlOrMeta+a');
    await page.keyboard.insertText('@main def main: IO[Unit] = Ui.run(_ => View.text(1))');
    await reveal(signalRun);
    await signalRun.click();
    await page.waitForFunction(() => Module.textBlocks?.some(block => /expected String/.test(block.text)));
    await page.getByRole('tab', {name: 'Cover', exact: true}).click();
    await expectSection('cover');
    await page.waitForFunction(() => {
      const snap = Module.ccall('sz_web_snapshot', 'string', [], []);
      return snap.includes('text:leftFirst: L must win') &&
        snap.includes('semantics:seed 0') && snap.includes('semantics:seed 128') &&
        snap.includes('text:first=R fail') && snap.includes('text:first=L pass');
    }, null, {timeout: 60000}).catch(async error => {
      console.error({cover: await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))});
      throw error;
    });
    await expectSnap('text:arms ');
    await expectSnap('text:live ');
    await expectSnap('text:mutant ');
    assert.equal(await page.getByRole('link', {name: 'Scuzz on GitHub', exact: true}).getAttribute('href'),
      'https://github.com/SeanCheatham/scuzz');
    await page.getByRole('button', {name: 'Copy', exact: true}).first().click();
    await page.getByRole('button', {name: 'Copied', exact: true}).first().waitFor();
    if (browserType === chromium) {
      const copied = await page.evaluate(() => navigator.clipboard.readText());
      assert(copied.includes('Queue.offer'), copied);
    }
    await page.evaluate(() => {
      window.writeClipboard = navigator.clipboard.writeText.bind(navigator.clipboard);
      navigator.clipboard.writeText = async () => { throw new Error('denied'); };
    });
    await page.getByRole('button', {name: 'Copied', exact: true}).first().click();
    await page.getByRole('button', {name: 'Copy failed', exact: true}).first().waitFor();
    await page.evaluate(() => { navigator.clipboard.writeText = window.writeClipboard; });
    // Browser commands must retain their default action.
    assert.deepEqual(await page.evaluate(() => ['f', '+', '-', '0', 'r', 'l'].map(key => {
      const event = new KeyboardEvent('keydown', {key, ctrlKey: true, bubbles: true, cancelable: true});
      Module.canvas.dispatchEvent(event); return event.defaultPrevented;
    })), Array(6).fill(false));
    assert.deepEqual(await page.evaluate(() => {
      const wheel = new WheelEvent('wheel', {bubbles: true, cancelable: true, deltaY: 10, clientX: 40, clientY: 80});
      window.dispatchEvent(wheel);
      const zoom = new WheelEvent('wheel', {bubbles: true, cancelable: true, deltaY: 10, ctrlKey: true});
      window.dispatchEvent(zoom);
      return [wheel.defaultPrevented, zoom.defaultPrevented];
    }), [true, false]);
    if (mobile && browserType === chromium) {
      assert.equal(await page.evaluate(() => {
        const touch = new Touch({identifier: 1, target: Module.canvas, clientX: 40, clientY: 80});
        const move = new TouchEvent('touchmove', {bubbles: true, cancelable: true, touches: [touch], changedTouches: [touch]});
        Module.canvas.dispatchEvent(move);
        return move.defaultPrevented;
      }), true);
    }
    if (browserType === chromium) {
      const cdp = await context.newCDPSession(page);
      const listeners = async expression => {
        const {result: {objectId}} = await cdp.send('Runtime.evaluate', {expression, returnByValue: false});
        return (await cdp.send('DOMDebugger.getEventListeners', {objectId})).listeners;
      };
      const wheel = (await listeners('window')).filter(listener => listener.type === 'wheel');
      const touch = (await listeners('Module.canvas')).filter(listener => listener.type === 'touchmove');
      assert(wheel.some(listener => listener.passive === false), JSON.stringify(wheel));
      assert(touch.some(listener => listener.passive === false), JSON.stringify(touch));
      await cdp.detach();
    }
    const runTab = page.getByRole('tab', {name: 'Run', exact: true});
    await reveal(runTab);
    await runTab.focus();
    await page.keyboard.press('End');
    await page.keyboard.press('Enter');
    await expectSection('cover');
    await page.goto(url + '?preview=1#stage=search');
    await expectSection('search');
    await page.reload(); await expectSection('search');
    assert.equal(new URL(page.url()).search, '?preview=1');
    await page.evaluate(() => { location.hash = 'stage=missing'; });
    await page.waitForFunction(() => location.hash === '#stage=search');
    if (mobile) {
      await page.getByRole('tab', {name: 'Run', exact: true}).tap();
      await expectSection('run');
      assert.equal(await page.evaluate(() => getComputedStyle(Module.canvas).touchAction), 'pinch-zoom');
      if (browserType === chromium) {
        const cdp = await context.newCDPSession(page);
        await cdp.send('Emulation.setPageScaleFactor', {pageScaleFactor: 1.5});
        await page.waitForFunction(() => visualViewport.scale > 1);
        await cdp.send('Emulation.setPageScaleFactor', {pageScaleFactor: 1});
        assert(await page.evaluate(() => {
          const touches = [1, 2].map(identifier => new Touch({identifier, target: Module.canvas, clientX: identifier * 100, clientY: 300}));
          const event = new TouchEvent('touchmove', {touches, changedTouches: touches, bubbles: true, cancelable: true});
          return Module.canvas.dispatchEvent(event);
        }));
        await cdp.detach();
      }
    }
    assert.deepEqual(errors, []);
    console.log(`web: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function main() {
  const directory = path.resolve(process.argv[2]);
  for (const file of ['index.html', 'app.js', 'app.wasm']) assert(fs.existsSync(path.join(directory, file)), `missing web asset: ${file}`);
  const server = http.createServer((request, response) => {
    const pathname = new URL(request.url, 'http://localhost').pathname;
    const relative = pathname === '/scuzz/' ? 'index.html' : pathname.replace(/^\/scuzz\//, '');
    if (!['index.html', 'app.js', 'app.wasm'].includes(relative)) { response.writeHead(404).end(); return; }
    response.setHeader('Content-Type', relative.endsWith('.wasm') ? 'application/wasm' : relative.endsWith('.js') ? 'text/javascript' : 'text/html');
    response.end(fs.readFileSync(path.join(directory, relative)));
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const url = `http://127.0.0.1:${server.address().port}/scuzz/`;
    for (const type of [chromium, firefox, webkit]) {
      if (process.argv[3] && process.argv[3] !== type.name()) continue;
      await check(type, url, false);
      if (type !== firefox) await check(type, url, true);
    }
  } finally { await new Promise(resolve => server.close(resolve)); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
