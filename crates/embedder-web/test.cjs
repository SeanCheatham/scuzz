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
    if (browserType === chromium) await context.grantPermissions(['clipboard-read', 'clipboard-write']);
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
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
    await page.goto(url);
    await expectText('text:Start');
    assert.equal(await page.title(), 'Scuzz Docs');
    assert.equal(await page.getByRole('heading', {name: 'Start', level: 1}).count(), 1);
    assert.equal(await page.getByRole('link').count(), 11);
    assert.equal(await page.getByRole('region', {name: 'App bar'}).count(), 1);
    await page.getByRole('button', {name: 'Add one', exact: true}).focus();
    await page.getByRole('button', {name: 'Add one', exact: true}).click();
    await expectText('text:Count: 1');
    const headings = {Install: 'Install', Language: 'Language', GUI: 'GUI', Verify: 'Verify', Web: 'Web'};
    for (const [label, heading] of Object.entries(headings)) {
      await page.getByRole('link', {name: label, exact: true}).click();
      await expectText('text:' + heading);
      assert.equal(new URL(page.url()).hash, '#section=' + label.toLowerCase());
      assert.equal(await page.getByRole('link', {name: label, exact: true}).getAttribute('aria-current'), 'page');
      assert.equal(await page.getByRole('button', {name: 'Add one', exact: true}).count(), 0);
      if (label === 'Verify') {
        // Sibling paragraphs and code blocks keep constant left margins.
        const margins = await page.evaluate(() => {
          const left = prefix => Module.textBlocks.find(block => block.text.startsWith(prefix)).lines[0].x;
          return {
            paragraphs: ['Scuzz does not use', 'A def with one Timeline', 'Verdict.alwaysHas', 'Zero iterations', 'Simulation is hermetic'].map(left),
            code: ['def bump', 'scuzz fuzz --iterations 16', 'scuzz fuzz --iterations 0'].map(left)
          };
        });
        for (const values of Object.values(margins)) assert.equal(new Set(values).size, 1, JSON.stringify(margins));
      }
    }
    await page.goBack(); await expectText('text:Verify');
    await page.goForward(); await expectText('text:Web');
    await page.getByRole('link', {name: 'Start', exact: true}).click();
    await expectText('text:Count: 1');
    await page.getByRole('button', {name: 'Reset', exact: true}).click();
    await expectText('text:Count: 0');

    // A real anchor keeps modified clicks and link addresses in the browser.
    const install = page.getByRole('link', {name: 'Install', exact: true});
    assert.equal(await install.getAttribute('href'), '#section=install');
    if (!mobile) {
      const popupReady = context.waitForEvent('page');
      await install.click({button: 'middle'});
      const popup = await popupReady;
      await popup.waitForFunction(() => Module.currentSection === 'install');
      await popup.close();
      await page.bringToFront();
    }
    await install.click(); await expectText('text:Install');
    const codeRow = await page.evaluate(() => {
      const text = [...document.querySelectorAll('.text span')].find(node => node.textContent.startsWith('curl -fsSL'));
      const line = text.getBoundingClientRect();
      const button = document.querySelector('[aria-label="Copy"]');
      const box = button.getBoundingClientRect();
      return {line: {x: line.x, y: line.y, width: line.width}, button: {x: box.x, y: box.y, bottom: box.bottom}};
    });
    assert(codeRow.line.y >= codeRow.button.y && codeRow.line.y < codeRow.button.bottom);
    assert(codeRow.line.x + codeRow.line.width <= codeRow.button.x);

    await page.getByRole('button', {name: 'Copy', exact: true}).first().click();
    await page.getByRole('button', {name: 'Copied', exact: true}).first().waitFor();
    if (browserType === chromium) assert.equal(await page.evaluate(() => navigator.clipboard.readText()),
      'curl -fsSL https://github.com/SeanCheatham/scuzz/releases/latest/download/install.sh | sh');

    // Clipboard failure keeps the source available and reports failure.
    await page.evaluate(() => {
      window.writeClipboard = navigator.clipboard.writeText.bind(navigator.clipboard);
      navigator.clipboard.writeText = async () => { throw new Error('denied'); };
    });
    await page.getByRole('button', {name: 'Copied', exact: true}).first().click();
    await page.getByRole('button', {name: 'Copy failed', exact: true}).first().waitFor();
    await page.evaluate(() => { navigator.clipboard.writeText = window.writeClipboard; });
    if (!mobile) {
      const line = page.locator('.text span').filter({hasText: 'curl -fsSL'}).first();
      const box = await line.boundingBox();
      await page.mouse.move(box.x + 1, box.y + box.height / 2);
      await page.mouse.down();
      await page.mouse.move(box.x + 80, box.y + box.height / 2, {steps: 10});
      await page.mouse.up();
      assert((await page.evaluate(() => getSelection().toString())).startsWith('curl'));
    }
    const source = await page.evaluate(() => {
      const first = [...document.querySelectorAll('.text span')].find(line => line.textContent.startsWith('curl -fsSL'));
      const lines = [...first.parentElement.children];
      const range = document.createRange();
      range.setStart(first.firstChild, 0); range.setEnd(lines.at(-1).firstChild, lines.at(-1).textContent.length);
      getSelection().removeAllRanges(); getSelection().addRange(range);
      const event = new ClipboardEvent('copy', {clipboardData: new DataTransfer(), bubbles: true, cancelable: true});
      document.dispatchEvent(event);
      return [event.clipboardData.getData('text/plain'), Module.textBlocks[first.block].text];
    });
    assert.equal(source[0], source[1]);
    await page.evaluate(() => getSelection().removeAllRanges());
    // Browser commands must retain their default action.
    assert.deepEqual(await page.evaluate(() => ['f', '+', '-', '0', 'r', 'l'].map(key => {
      const event = new KeyboardEvent('keydown', {key, ctrlKey: true, bubbles: true, cancelable: true});
      Module.canvas.dispatchEvent(event); return event.defaultPrevented;
    })), Array(6).fill(false));
    await install.focus(); await page.keyboard.press('ArrowDown'); await page.keyboard.press('Enter');
    await expectText('text:Language');

    await page.getByRole('link', {name: 'GUI', exact: true}).click();
    await expectText('text:GUI');
    const headingTop = await page.getByRole('heading', {name: 'GUI', exact: true})
      .evaluate(node => node.firstElementChild.getBoundingClientRect().top);
    const positions = await page.evaluate(() => JSON.stringify(Module.textBlocks.map(block => block.lines.map(line => line.y))));
    if (mobile) {
      await page.evaluate(() => {
        const target = [...document.querySelectorAll('.text span')].find(node => node.textContent.startsWith('A View describes'));
        const box = target.getBoundingClientRect();
        const touch = y => ({identifier: 1, target, clientX: box.x + 10, clientY: y});
        const send = (type, touches) => {
          const event = new Event(type, {bubbles: true, cancelable: true});
          Object.defineProperty(event, 'touches', {value: touches});
          target.dispatchEvent(event);
        };
        send('touchstart', [touch(box.y + 70)]);
        send('touchmove', [touch(box.y + 10)]);
        send('touchend', []);
      });
    } else {
      const box = await page.locator('.text span').filter({hasText: 'A View describes'}).first().boundingBox();
      await page.mouse.move(box.x + 10, box.y + 10); await page.mouse.wheel(0, 100);
    }
    await page.waitForFunction(before => JSON.stringify(Module.textBlocks.map(block => block.lines.map(line => line.y))) !== before, positions);
    assert.equal(await page.getByRole('heading', {name: 'GUI', exact: true})
      .evaluate(node => node.firstElementChild.getBoundingClientRect().top), headingTop);
    const field = page.getByRole('textbox', {name: 'Your text', exact: true});
    // Focus must reveal the field in its shared scroll container.
    await field.focus();
    await page.waitForFunction(() => {
      const field = document.querySelector('input.edit');
      const box = field.getBoundingClientRect(); return box.y >= 0 && box.bottom <= innerHeight;
    });
    await page.keyboard.insertText('café 🐈');
    await expectText('text:You typed: café 🐈');
    await field.evaluate(node => {
      node.dispatchEvent(new CompositionEvent('compositionstart', {bubbles: true}));
      node.value = 'café 🐈日本';
      node.dispatchEvent(new CompositionEvent('compositionupdate', {data: '日本', bubbles: true}));
    });
    assert(!(await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))).includes('text:You typed: café 🐈日本'));
    await field.evaluate(node => {
      node.dispatchEvent(new CompositionEvent('compositionend', {data: '日本', bubbles: true}));
      node.dispatchEvent(new InputEvent('input', {data: '日本', inputType: 'insertCompositionText', bubbles: true}));
    });
    await expectText('text:You typed: café 🐈日本');
    if (browserType === chromium) {
      await page.evaluate(() => navigator.clipboard.writeText('paste 😀'));
      await field.focus(); await page.keyboard.press('Control+a'); await page.keyboard.press('Control+v');
      await expectText('text:You typed: paste 😀');
    }
    const editor = page.getByRole('textbox', {name: 'editor', exact: true});
    await editor.focus(); await page.keyboard.insertText('one\ntwo 🌍');
    await expectText('text:Notes: one\ntwo 🌍');
    const savedText = await field.inputValue();
    const liveTab = page.getByRole('tab', {name: 'Live example', exact: true});
    const sourceTab = page.getByRole('tab', {name: 'Source', exact: true});
    assert.equal(await page.getByRole('tablist').count(), 1);
    assert.equal(await liveTab.getAttribute('tabindex'), '0');
    assert.equal(await sourceTab.getAttribute('tabindex'), '-1');
    await liveTab.focus();
    await page.keyboard.press('End');
    assert(await sourceTab.evaluate(node => node === document.activeElement));
    assert.equal(await liveTab.getAttribute('aria-selected'), 'true');
    await page.keyboard.press('Enter');
    await page.getByRole('tabpanel', {name: 'Source', exact: true}).waitFor();
    const sourcePanel = page.getByRole('tabpanel', {name: 'Source', exact: true});
    assert((await sourcePanel.locator('.text').textContent()).endsWith('  } yield ()'));
    assert.equal(await field.count(), 0);
    assert.equal(await editor.count(), 0);
    assert.equal(new URL(page.url()).hash, '#section=gui');
    assert(await sourceTab.evaluate(node => document.getElementById(node.getAttribute('aria-controls')).getAttribute('role') === 'tabpanel'));
    await page.keyboard.press('Tab');
    assert(await page.getByRole('tabpanel', {name: 'Source', exact: true}).evaluate(node => node === document.activeElement));
    await page.getByRole('link', {name: 'Start', exact: true}).click();
    await page.getByRole('button', {name: 'Try GUI', exact: true}).click();
    await page.getByRole('tabpanel', {name: 'Source', exact: true}).waitFor();
    await sourceTab.focus(); await page.keyboard.press('ArrowRight'); await page.keyboard.press('Space');
    await page.getByRole('tabpanel', {name: 'Live example', exact: true}).waitFor();
    assert.equal(await field.inputValue(), savedText);
    assert.equal(await editor.inputValue(), 'one\ntwo 🌍');
    await liveTab.focus(); await page.keyboard.press('End'); await page.keyboard.press('Home');
    assert(await liveTab.evaluate(node => node === document.activeElement));
    await page.setViewportSize({width: 390, height: 400});
    await field.focus();
    await page.waitForFunction(() => {
      const field = document.querySelector('input.edit');
      const box = field.getBoundingClientRect();
      return box.top >= 0 && box.bottom <= 400 && document.elementFromPoint(box.x + box.width / 2, box.y + box.height / 2) === field;
    });
    await page.setViewportSize({width: mobile ? 390 : 1000, height: 720});
    await page.getByRole('link', {name: 'Web', exact: true}).click();
    await expectText('text:Web');
    if (mobile) {
      await page.getByRole('link', {name: 'Install', exact: true}).tap();
      await expectText('text:Install');
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
    await page.goto(url + '?preview=1#section=language');
    await expectText('text:Language');
    await page.reload(); await expectText('text:Language');
    assert.equal(new URL(page.url()).search, '?preview=1');
    await page.evaluate(() => { location.hash = 'section=missing'; });
    await page.waitForFunction(() => location.hash === '#section=language');
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
