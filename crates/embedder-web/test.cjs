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
    page.on('pageerror', error => errors.push(error.stack));
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
    await expectText('text:Scuzz Lang');
    await expectSection('intro');
    await expectText('text:Intro 1/8');
    assert.equal(await page.title(), 'Scuzz');
    {
      await page.waitForFunction(() => !Module.ccall('sz_web_motion_pending', 'number', [], []));
      const paints = await page.evaluate(() => Module.ccall('sz_web_paints', 'number', [], []));
      const pumps = await page.evaluate(() => Module.ccall('sz_web_pumps', 'number', [], []));
      const frames = await page.evaluate(() => window.rafRequests);
      assert(paints >= 1, 'first paint');
      await page.evaluate(() => new Promise(resolve => setTimeout(resolve, 400)));
      assert.equal(await page.evaluate(() => Module.ccall('sz_web_paints', 'number', [], [])), paints);
      assert.equal(await page.evaluate(() => Module.ccall('sz_web_pumps', 'number', [], [])), pumps);
      assert.equal(await page.evaluate(() => window.rafRequests), frames, 'idle frame loop');
    }
    assert.equal(await page.getByRole('heading', {name: 'Intro 1/8', level: 1}).count(), 1);
    assert.equal(await page.getByRole('navigation', {name: 'Breadcrumb'}).count(), 0);
    assert.equal(await page.getByRole('region', {name: 'App bar'}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'Intro', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'Run', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'View', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'State', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'Cover', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'Mutation', exact: true}).count(), 1);
    assert.equal(await page.getByRole('button', {name: 'Add one', exact: true}).count(), 0);
    assert.equal(await page.getByRole('img').count(), 0);
    const introContinue = page.getByRole('button', {name: 'Continue', exact: true});
    await reveal(introContinue);
    await introContinue.click();
    await expectSection('run');
    await expectText('text:Run inc');
    const run = page.getByRole('button', {name: 'Run', exact: true});
    await reveal(run);
    await run.click();
    await expectText('text:1');
    {
      const tryEditor = page.getByRole('textbox', {name: 'editor', exact: true});
      const original = await tryEditor.inputValue();
      const overlay = await tryEditor.evaluate(node => {
        const computed = getComputedStyle(node);
        return {color: computed.color, background: computed.backgroundColor};
      });
      assert.equal(overlay.color, 'rgba(0, 0, 0, 0)', JSON.stringify(overlay));
      assert.equal(overlay.background, 'rgba(0, 0, 0, 0)', JSON.stringify(overlay));
      await tryEditor.focus(); await page.keyboard.press('ControlOrMeta+a');
      await page.keyboard.insertText(Array.from({length: 40}, (_, i) => `line ${i}`).join('\n'));
      const scrolled = await tryEditor.evaluate(node => {
        const rect = node.getBoundingClientRect();
        const before = node.scrollTop;
        const wheel = new WheelEvent('wheel', {bubbles: true, cancelable: true, deltaY: 120,
          clientX: rect.left + 24, clientY: rect.top + 24});
        node.dispatchEvent(wheel);
        return {before, after: node.scrollTop, height: node.scrollHeight, client: node.clientHeight};
      });
      assert(scrolled.height > scrolled.client, JSON.stringify(scrolled));
      assert(scrolled.after > scrolled.before, JSON.stringify(scrolled));
      if (mobile && browserType === chromium) {
        const swiped = await tryEditor.evaluate(node => {
          node.scrollTop = 0;
          const rect = node.getBoundingClientRect();
          const x = rect.left + 24;
          const y0 = rect.top + 80;
          const y1 = rect.top + 20;
          const before = node.scrollTop;
          const startTouch = new Touch({identifier: 1, target: node, clientX: x, clientY: y0});
          const moveTouch = new Touch({identifier: 1, target: node, clientX: x, clientY: y1});
          node.dispatchEvent(new TouchEvent('touchstart', {bubbles: true, cancelable: true,
            touches: [startTouch], changedTouches: [startTouch]}));
          const move = new TouchEvent('touchmove', {bubbles: true, cancelable: true,
            touches: [moveTouch], changedTouches: [moveTouch]});
          node.dispatchEvent(move);
          return {before, after: node.scrollTop, prevented: move.defaultPrevented};
        });
        assert(swiped.after > swiped.before, JSON.stringify(swiped));
        assert.equal(swiped.prevented, true);
      }
      await tryEditor.focus(); await page.keyboard.press('ControlOrMeta+a');
      await page.keyboard.insertText(original);
    }
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
    await page.waitForFunction(() => {
      const snap = Module.ccall('sz_web_snapshot', 'string', [], []);
      return snap.includes('text:A claim reads a Timeline') &&
        snap.includes('text:A usual test names one input and one expected output.') &&
        snap.includes('text:A claim names a rule.') &&
        snap.includes('text:The rule must hold for a recorded run.') &&
        snap.includes('text:You write the claim once.') &&
        snap.includes('text:Scuzz chooses the seeds.') &&
        snap.includes('text:Each seed is one schedule.') &&
        snap.includes('text:Scuzz records that schedule as a Timeline.') &&
        snap.includes('text:The claim reads that timeline after the run.') &&
        snap.includes('text:The claim does not sit inside the program.') &&
        snap.includes('text:R won on seed 0.') &&
        snap.includes('text:The rule says L must win.') &&
        snap.includes('text:L won on seed 128.') &&
        snap.includes('def leftFirst(t: Timeline): Verdict =') &&
        snap.includes('semantics:seed 0') && snap.includes('semantics:seed 128') &&
        snap.includes('text:first=R fail') && snap.includes('text:first=L pass');
    }, null, {timeout: 60000}).catch(async error => {
      console.error({check: await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))});
      throw error;
    });
    await expectSection('check');
    await expectText('text:Check 4/8');
    assert.equal(await page.getByRole('tab', {name: 'Main.scuzz', exact: true}).count(), 1);
    assert.equal(await page.getByRole('tab', {name: 'count.scuzz_verify', exact: true}).count(), 1);
    await expectEditor('oracle incAdds');
    const check = page.getByRole('button', {name: 'Check', exact: true});
    await reveal(check);
    await check.click();
    await expectText('text:true');
    const stateTab = page.getByRole('tab', {name: 'State', exact: true});
    await reveal(stateTab);
    await stateTab.click();
    await expectSection('state');
    assert.equal(new URL(page.url()).hash, '#stage=state');
    await expectText('text:State 5/8');
    const stateRun = page.getByRole('button', {name: 'Run', exact: true});
    await reveal(stateRun);
    await stateRun.click();
    await expectText('text:Clicks: 0');
    const plusState = page.getByRole('button', {name: '+1', exact: true});
    await reveal(plusState);
    await plusState.click();
    await expectText('text:Clicks: 1');
    assert.equal(await page.getByRole('button', {name: 'Continue', exact: true}).count(), 1);
    {
      await page.waitForFunction(() => !Module.ccall('sz_web_motion_pending', 'number', [], []));
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
    await page.getByRole('tab', {name: 'State', exact: true}).click();
    await expectText('text:Clicks: 1');
    await reveal(stateRun);
    await stateRun.click();
    await expectText('text:Clicks: 0');
    const tryEditor = page.getByRole('textbox', {name: 'editor', exact: true});
    const trySource = await tryEditor.inputValue();
    assert(trySource.includes('Clicks: $n'));
    await tryEditor.focus(); await page.keyboard.press('ControlOrMeta+a');
    await page.keyboard.insertText(trySource.replace('Clicks: $n', 'Taps: $n'));
    await reveal(stateRun);
    await stateRun.click();
    await expectText('text:Taps: 0');
    const plusMounted = page.getByRole('button', {name: '+1', exact: true});
    await reveal(plusMounted);
    await plusMounted.click();
    await expectText('text:Taps: 1');
    await tryEditor.focus(); await page.keyboard.press('ControlOrMeta+a');
    await page.keyboard.insertText('@main def main: IO[Unit] = Ui.run(_ => View.text(1))');
    await reveal(stateRun);
    await stateRun.click();
    await page.waitForFunction(() => Module.textBlocks?.some(block => /expected String/.test(block.text)));
    await page.getByRole('tab', {name: 'Cover', exact: true}).click();
    await expectSection('cover');
    await page.waitForFunction(() => {
      const snap = Module.ccall('sz_web_snapshot', 'string', [], []);
      return snap.includes('text:Paint reached lines') &&
        snap.includes('text:hits ') && snap.includes('semantics:cover-hit') &&
        !snap.includes('semantics:seed 0') && !snap.includes('text:leftFirst: L must win');
    }, null, {timeout: 60000}).catch(async error => {
      console.error({cover: await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))});
      throw error;
    });
    await page.getByRole('tab', {name: 'Mutation', exact: true}).click();
    await expectSection('mutation');
    await expectSnap('text:live source');
    await expectSnap('text:mutant source');
    await expectSnap('text:- ');
    await expectSnap('text:+ ');
    await page.waitForFunction(() => Module.ccall('sz_web_snapshot', 'string', [], []).includes('text:incAdds reject'),
      null, {timeout: 60000}).catch(async error => {
      console.error({mutation: await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []))});
      throw error;
    });
    assert.equal(await page.getByRole('link', {name: 'Scuzz on GitHub', exact: true}).getAttribute('href'),
      'https://github.com/SeanCheatham/scuzz');
    await page.getByRole('tab', {name: 'Check', exact: true}).click();
    await expectSection('check');
    const copy = page.getByRole('button', {name: 'Copy', exact: true}).first();
    await copy.waitFor({timeout: 60000});
    await reveal(copy);
    await copy.click();
    const copied = page.getByRole('button', {name: 'Copied', exact: true}).first();
    await copied.waitFor();
    if (browserType === chromium) {
      const copied = await page.evaluate(() => navigator.clipboard.readText());
      assert(copied.includes('Queue.offer'), copied);
    }
    await page.evaluate(() => {
      window.writeClipboard = navigator.clipboard.writeText.bind(navigator.clipboard);
      navigator.clipboard.writeText = async () => { throw new Error('denied'); };
    });
    await reveal(copied);
    await copied.click();
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
    const selectedBefore = await page.getByRole('tab', {name: 'Check', exact: true}).getAttribute('aria-selected');
    await page.keyboard.press('ArrowRight');
    assert.equal(await page.evaluate(() => document.activeElement.getAttribute('aria-label')), 'View');
    await page.waitForFunction(() => document.activeElement.tabIndex === 0 &&
      document.activeElement.closest('[role=tablist]').querySelectorAll('[tabindex="0"]').length === 1);
    assert.equal(await page.getByRole('tab', {name: 'Check', exact: true}).getAttribute('aria-selected'), selectedBefore);
    await page.keyboard.press('ArrowDown');
    assert.equal(await page.evaluate(() => document.activeElement.getAttribute('aria-label')), 'View');
    await page.keyboard.press('Tab');
    assert.notEqual(await page.evaluate(() => document.activeElement.getAttribute('role')), 'tab');
    await page.keyboard.press('Shift+Tab');
    assert.equal(await page.evaluate(() => document.activeElement.getAttribute('aria-label')), 'View');
    await page.keyboard.press('End');
    await page.keyboard.press('Enter');
    await expectSection('mutation');
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

async function checkLayout(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 640, height: 480},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready &&
      Module.ccall('sz_web_snapshot', 'string', [], []).includes('image:Color sample'));
    let previousValue = 0;
    for (const width of [640, 300, 180, 640]) {
      await page.setViewportSize({width, height: 480});
      await page.waitForFunction(width => Module.canvas.width === width * devicePixelRatio, width);
      await page.waitForFunction(() => Module.webFrame.scale === devicePixelRatio);
      await page.waitForFunction(value => Module.ccall('sz_web_snapshot', 'string', [], [])
        .includes(`text:Selected: ${value}`), previousValue);
      const image = page.getByRole('img', {name: 'Color sample', exact: true});
      assert.equal(await image.count(), 1);
      const imageBox = await image.boundingBox();
      const labelBox = await page.getByText(`Selected: ${previousValue}`, {exact: true}).boundingBox();
      assert(imageBox && labelBox);
      if (width >= 392) assert(Math.abs(imageBox.y - labelBox.y) < 40);
      else assert(labelBox.y > imageBox.y + imageBox.height);
      const colors = await image.evaluate(node => {
        const item = node.item;
        const size = Math.min(item.w, item.h);
        const x = item.x + (item.w - size) / 2;
        const y = item.y + (item.h - size) / 2;
        const ctx = Module.canvas.getContext('2d');
        return [[0.05, 0.05], [0.95, 0.05], [0.05, 0.95], [0.95, 0.95]].map(([dx, dy]) =>
          [...ctx.getImageData(Math.floor(x + size * dx), Math.floor(y + size * dy), 1, 1).data]);
      });
      assert(colors[0][0] > colors[0][1] + 100 && colors[0][0] > colors[0][2] + 100, colors);
      assert(colors[1][1] > colors[1][0] + 80 && colors[1][1] > colors[1][2] + 80, colors);
      assert(colors[2][0] > 200 && colors[2][1] > 200 && colors[2][2] > 200, colors);
      assert(colors[3][2] > colors[3][0] + 100 && colors[3][2] > colors[3][1] + 100, colors);
      for (const [label, value] of [['First', 1], ['Second', 2], ['Third', 3]]) {
        const button = page.getByRole('button', {name: label, exact: true});
        const box = await button.boundingBox();
        assert(box && box.x >= 0 && box.y >= 0 && box.x + box.width <= width && box.y + box.height <= 480, box);
        if (mobile) await button.tap(); else await button.click();
        await page.waitForFunction(value => Module.ccall('sz_web_snapshot', 'string', [], [])
          .includes(`text:Selected: ${value}`), value);
        previousValue = value;
      }
    }
    assert.deepEqual(errors, []);
    console.log(`web layout: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkList(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 640, height: 480},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready &&
      Module.ccall('sz_web_snapshot', 'string', [], []).includes('text:Item 0'));
    const fields = page.getByRole('textbox');
    assert(await fields.count() <= 5);
    const first = fields.first();
    await first.fill('kept');
    const id = await first.getAttribute('data-view');
    await page.waitForFunction(id => Module.webFrame.items.some(item =>
      item.id === Number(id) && item.value === 'kept'), id);
    await page.setViewportSize({width: 300, height: 480});
    await page.waitForFunction(() => Module.canvas.width === 300 * devicePixelRatio);
    assert.equal(await fields.first().inputValue(), 'kept');
    assert.equal(await fields.first().getAttribute('data-view'), id);
    const oldIds = await fields.evaluateAll(nodes => nodes.map(node => node.dataset.view));
    if (mobile) {
      await page.evaluate(() => {
        const canvas = Module.canvas;
        const send = (type, y) => {
          const touch = {identifier: 1, target: canvas, clientX: 20, clientY: y};
          const ended = type === 'touchend';
          const event = new Event(type, {bubbles: true, cancelable: true});
          Object.defineProperties(event, {touches: {value: ended ? [] : [touch]},
            targetTouches: {value: ended ? [] : [touch]}, changedTouches: {value: [touch]}});
          canvas.dispatchEvent(event);
        };
        send('touchstart', 450);
        send('touchmove', 150);
        send('touchend', 150);
      });
    } else {
      await page.mouse.move(150, 250);
      await page.mouse.wheel(0, 600);
    }
    await page.waitForFunction(id => !Module.nodes.has(Number(id)) &&
      Module.webFrame.texts.every(item => item.text !== 'Item 0'), id);
    const maximumRows = await page.evaluate(() =>
      Math.ceil(Module.canvas.height / (120 * Module.webFrame.scale)) + 3);
    assert(await fields.count() <= maximumRows);
    const newIds = await fields.evaluateAll(nodes => nodes.map(node => node.dataset.view));
    assert(!newIds.includes(id), {oldIds, newIds});
    assert.equal(await page.evaluate(id => Module.ccall('sz_web_activate', 'number', ['number'], [Number(id)]), id), 0);
    const expected = await page.evaluate(() => Module.webFrame.texts.filter(item => /^Item \d+$/.test(item.text))
      .map(item => `Item ${9999 - Number(item.text.slice(5))}`));
    await page.getByRole('button', {name: 'Reverse', exact: true}).click();
    await page.waitForFunction(expected => JSON.stringify(Module.webFrame.texts.filter(item => /^Item \d+$/.test(item.text)).map(item => item.text))
      === JSON.stringify(expected), expected);
    assert(await fields.count() <= maximumRows);
    assert.deepEqual(errors, []);
    console.log(`web list: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkMeasured(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame.texts.some(item => item.text === 'Item 0'));
    const fields = page.getByRole('textbox', {name: 'Note', exact: true});
    assert(await fields.count() < 12);
    const heights = await page.evaluate(() => {
      const titles = Module.webFrame.texts.filter(item => /^Item \d+$/.test(item.text));
      return titles.slice(1).map((item, i) => Math.round((item.lines[0].y - titles[i].lines[0].y) / Module.webFrame.scale));
    });
    assert(new Set(heights).size > 1, heights);
    const first = fields.first();
    await first.fill('kept');
    const firstId = await first.getAttribute('data-view');
    await page.setViewportSize({width: 300, height: 720});
    await page.waitForFunction(() => Module.canvas.width === 300 * devicePixelRatio);
    assert.equal(await fields.first().getAttribute('data-view'), firstId);
    assert.equal(await fields.first().inputValue(), 'kept');
    await page.evaluate(() => Module.canvas.focus());
    if (mobile) {
      await page.evaluate(() => {
        const canvas = Module.canvas;
        const send = (type, y) => {
          const touch = {identifier: 1, target: canvas, clientX: 20, clientY: y};
          const ended = type === 'touchend';
          const event = new Event(type, {bubbles: true, cancelable: true});
          Object.defineProperties(event, {touches: {value: ended ? [] : [touch]},
            targetTouches: {value: ended ? [] : [touch]}, changedTouches: {value: [touch]}});
          canvas.dispatchEvent(event);
        };
        for (let i = 0; i < 3; i++) {
          send('touchstart', 650); send('touchmove', 250); send('touchend', 250);
        }
      });
    } else {
      await page.mouse.move(150, 450);
      await page.mouse.wheel(0, 1200);
    }
    await page.waitForFunction(id => !Module.nodes.has(Number(id)), firstId);
    assert(await fields.count() < 12);
    assert.equal(await page.evaluate(id => Module.ccall('sz_web_activate', 'number', ['number'], [Number(id)]), firstId), 0);
    const anchor = await page.evaluate(() => Module.webFrame.texts.find(item => /^Item \d+$/.test(item.text)).text);
    await page.setViewportSize({width: 360, height: 720});
    await page.waitForFunction(() => Module.canvas.width === 360 * devicePixelRatio);
    assert.equal(await page.evaluate(() => Module.webFrame.texts.find(item => /^Item \d+$/.test(item.text)).text), anchor);
    const retainedId = await fields.nth(1).getAttribute('data-view');
    const retained = page.locator(`[data-view="${retainedId}"]`);
    await retained.focus();
    await retained.fill('retained');
    await page.getByRole('button', {name: 'Reverse', exact: true}).click();
    await page.waitForFunction(id => {
      const keys = Module.webFrame.texts.filter(item => /^Item \d+$/.test(item.text)).map(item => Number(item.text.slice(5)));
      return keys.length > 1 && keys.every((key, i) => i === 0 || key < keys[i - 1]) &&
        Module.nodes.has(Number(id)) && Module.nodes.get(Number(id)).value === 'retained';
    }, retainedId);
    assert(await fields.count() < 12);
    assert.deepEqual(errors, []);
    console.log(`web measured: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkText(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    const source = n => `Count: ${n}. One paragraph can keep a word across styles: interface. Use Text.concat for spans. Source text keeps spaces and λ🙂.\nThe next line keeps its explicit break.\n`;
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.textBlocks?.some(block => block.text.startsWith('Count: 0. ')));
    const title = await page.evaluate(() => Module.textBlocks.find(block => block.text === 'Styled text').lines
      .map(line => [line.font / Module.webFrame.scale, line.style, line.y]));
    assert.equal(title.length, 2);
    assert.deepEqual(title.map(line => line.slice(0, 2)), [[28, 1], [18, 2]]);
    assert.equal(title[0][2], title[1][2]);
    assert.equal(await page.evaluate(() => Module.textBlocks.some(block => block.text === '· · ·')), false);
    const copy = async (prefix, word) => page.evaluate(({prefix, word}) => {
      const block = Module.textBlocks.find(block => block.text.startsWith(prefix));
      const node = Module.nodes.get(block.id);
      const spans = [...node.children];
      let first = spans[0], last = spans.at(-1), start = 0, end = last.textContent.length;
      if (word) {
        const at = block.text.indexOf(word);
        first = spans.find(span => span.sourceStart <= at && span.sourceEnd > at);
        last = spans.find(span => span.sourceStart < at + word.length && span.sourceEnd >= at + word.length);
        start = at - first.sourceStart;
        end = at + word.length - last.sourceStart;
      }
      const range = document.createRange();
      range.setStart(first.firstChild, start);
      range.setEnd(last.firstChild, end);
      const selection = getSelection();
      selection.removeAllRanges(); selection.addRange(range);
      let copied = null;
      const event = new Event('copy', {bubbles: true, cancelable: true});
      Object.defineProperty(event, 'clipboardData', {value: {setData(type, text) { if (type === 'text/plain') copied = text; }}});
      document.dispatchEvent(event);
      selection.removeAllRanges();
      return copied;
    }, {prefix, word});
    assert.equal(await copy('Count: 0. '), source(0));
    assert.equal(await copy('Count: 0. ', 'interface'), 'interface');
    const styles = await page.evaluate(() => {
      const block = Module.textBlocks.find(block => block.text.startsWith('Count: 0. '));
      const at = block.text.indexOf('interface');
      const first = block.lines.find(line => line.start <= at && line.end > at);
      const last = block.lines.find(line => line.start <= at + 5 && line.end > at + 5);
      const node = Module.nodes.get(block.id);
      const colored = [...node.children].find(span => span.textContent.includes('face'));
      const preview = Module.textBlocks.find(block => block.text.startsWith('Count: 100. '));
      return {sameLine: first.y === last.y, italic: last.style & 2,
        ink: getComputedStyle(colored, '::selection').color,
        mono: block.lines.some(line => line.style & 4), underline: block.lines.some(line => line.style & 8),
        dots: preview.lines.at(-1).synthetic, cutoff: preview.lines.at(-1).end};
    });
    assert.equal(styles.sameLine, true);
    assert.equal(styles.italic, 2);
    assert.equal(styles.ink, 'rgb(160, 55, 35)');
    assert(styles.mono && styles.underline && styles.dots);
    assert.equal(await copy('Count: 100. '), source(100).slice(0, styles.cutoff));
    const code = 'Text.concat([' + 'Text.span("source"), '.repeat(39) + 'Text.span("source")])';
    assert.equal(await copy('Text.concat(['), code);
    await page.evaluate(() => {
      const line = Module.textBlocks.find(block => block.text.startsWith('Text.concat([')).lines[0];
      Module.ccall('sz_web_scroll', null, ['number', 'number', 'number', 'number'],
        [innerWidth / 2, (line.y - line.font / 2) / Module.webFrame.scale, 2500, 0]);
    });
    await page.waitForFunction(() => Module.textBlocks.find(block => block.text.startsWith('Text.concat([')).lines[0].x < -1000);
    const ink = await page.evaluate(() => {
      const line = Module.textBlocks.find(block => block.text.startsWith('Text.concat([')).lines[0];
      const canvas = Module.canvas;
      const pixels = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
      let count = 0;
      for (let y = Math.max(0, Math.floor(line.y - line.font)); y < Math.min(canvas.height, Math.ceil(line.y + line.font / 4)); y++)
        for (let x = Math.floor(40 * Module.webFrame.scale); x < canvas.width - 40 * Module.webFrame.scale; x++) {
          const at = (y * canvas.width + x) * 4;
          if (pixels[at] > 100 && pixels[at + 1] < 100 && pixels[at + 2] < 100) count++;
        }
      return count;
    });
    assert(ink > 0);
    assert.equal(await copy('Text.concat(['), code);

    await page.getByRole('textbox', {name: 'Draft', exact: true}).fill('retained');
    await page.getByRole('button', {name: 'Increment', exact: true}).click();
    await page.waitForFunction(() => Module.textBlocks.some(block => block.text.startsWith('Count: 1. ')));
    await page.setViewportSize({width: 300, height: 720});
    await page.waitForFunction(() => Module.canvas.width === Math.round(300 * devicePixelRatio));
    assert.equal(await copy('Count: 1. '), source(1));
    assert.equal(await page.getByRole('textbox', {name: 'Draft', exact: true}).inputValue(), 'retained');
    assert.deepEqual(errors, []);
    console.log(`web text: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkThemes(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.nodes.size > 0);
    const palettes = [
      ['Dark', 'rgb(243, 239, 227)', 'rgb(94, 100, 28)'],
      ['Light', 'rgb(36, 35, 31)', 'rgb(232, 239, 72)'],
      ['Custom', 'rgb(240, 245, 250)', 'rgb(60, 100, 120)']
    ];
    for (const [label, ink, selection] of palettes) {
      const field = page.getByRole('textbox', {name: label, exact: true});
      assert.equal(await field.evaluate(node => getComputedStyle(node).caretColor), ink);
      assert.equal(await field.evaluate(node => getComputedStyle(node, '::selection').backgroundColor), selection);
    }
    for (const [label, font, height, line] of [['Dark', 14, 40, 20], ['Light', 12, 28, 16], ['Custom', 22, 52, 32]]) {
      const field = page.getByRole('textbox', {name: label, exact: true});
      assert.equal(await field.evaluate(node => parseFloat(getComputedStyle(node).fontSize)), font);
      assert.equal(await field.evaluate(node => parseFloat(getComputedStyle(node).lineHeight)), line);
      assert.equal(await field.evaluate(node => node.getBoundingClientRect().height), height);
    }
    assert.deepEqual(await page.evaluate(() => {
      const footer = Module.textBlocks.find(block => block.text === 'Dark footer');
      const line = footer.lines[0];
      return [line.font / Module.webFrame.scale, line.line_h / Module.webFrame.scale];
    }), [14, 20]);
    const corner = await page.evaluate(() => Array.from(Module.canvas.getContext('2d').getImageData(1, 1, 1, 1).data));
    assert.deepEqual(corner, [36, 35, 31, 255]);
    await page.getByRole('textbox', {name: 'Dark', exact: true}).fill('Ada');
    await page.waitForFunction(() => [...Module.nodes.values()].filter(node => node.classList.contains('edit'))
      .every(node => node.value === 'Ada'));
    await page.getByRole('button', {name: 'Apply', exact: true}).nth(1).click();
    await page.waitForFunction(() => [...Module.nodes.values()].filter(node => node.getAttribute('role') === 'checkbox')
      .every(node => node.getAttribute('aria-checked') === 'true'));
    assert.equal(await page.getByRole('checkbox', {name: /^Updates /}).count(), 3);
    assert.deepEqual(errors, []);
    console.log(`web themes: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkMotionUnicode(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame?.transitions.length === 1);
    const prefix = 'a'.repeat(62), label = prefix + '中文';
    const snapshot = await page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []));
    assert(snapshot.startsWith('opacity:' + prefix + '=50\n'));
    assert(snapshot.includes('target:opacity:' + prefix + '=50\n'));
    assert(snapshot.includes('pumps:opacity:' + prefix + '=0\n'));
    assert(snapshot.includes('text:' + label + '\n'));
    assert(!snapshot.includes('\ufffd'), 'frame metadata preserves complete UTF-8 characters');
    assert(await page.evaluate(label => Module.textBlocks.some(block => block.text === label), label));
    assert.deepEqual(errors, []);
    console.log(`web motion Unicode: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function openMotionPage(page, url) {
  await page.addInitScript(() => {
    const frames = new Map(); let next = 0;
    const browserFrame = window.requestAnimationFrame.bind(window);
    window.motionBrowserFrame = () => new Promise(browserFrame);
    window.requestAnimationFrame = callback => { frames.set(++next, callback); return next; };
    window.cancelAnimationFrame = id => frames.delete(id);
    window.motionStep = () => {
      const callbacks = [...frames.values()]; frames.clear();
      window.motionCallbacks = callbacks.length;
      callbacks.forEach(callback => callback(performance.now()));
    };
  });
  await page.goto(url);
  const deadline = Date.now() + 30000;
  while (!await page.evaluate(() => {
    window.motionStep(); return Module.ready && Module.webFrame?.transitions.length === 1;
  })) {
    assert(Date.now() < deadline, 'motion application starts');
    await page.waitForTimeout(10);
  }
  await page.evaluate(() => document.fonts.ready);
}

async function checkMotion(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile, reducedMotion: 'no-preference'});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await openMotionPage(page, url);
    const frame = () => page.evaluate(() => Module.webFrame.transitions[0]);
    const advance = async count => {
      const values = [];
      for (let i = 0; i < count; i++) {
        const deadline = Date.now() + 30000;
        while (!await page.evaluate(() => {
          const before = Module.webFrame; window.motionStep(); return before !== Module.webFrame;
        })) {
          if (Date.now() >= deadline) throw new Error(JSON.stringify(await page.evaluate(() => ({frame: Module.webFrame.transitions, texts: Module.textBlocks, callbacks: window.motionCallbacks, ready: Module.ready, error: Module.lastError})), null, 2));
          await page.waitForTimeout(10);
        }
        values.push((await frame())?.value);
      }
      return values;
    };
    const click = async name => page.getByRole('button', {name, exact: true}).evaluate(node => node.click());
    const name = page.getByRole('textbox', {name: 'Name', exact: true});
    assert.equal((await frame()).value, 100);
    const sample = await page.evaluate(() => {
      const item = Module.webFrame.items.find(item => item.label === 'Opacity sample');
      const canvas = document.getElementById('canvas');
      return Array.from(canvas.getContext('2d').getImageData(Math.floor(item.x + item.w / 2), Math.floor(item.y + item.h / 2), 1, 1).data);
    });
    for (const [index, expected] of [127, 127, 255, 255].entries())
      assert(Math.abs(sample[index] - expected) <= 1, 'overlap composites before opacity: ' + sample);
    await click('Fade out'); await name.focus();
    assert.deepEqual(await advance(3), [88, 75, 63]);
    assert(await name.evaluate(node => document.activeElement === node));
    assert.equal(await name.inputValue(), 'Retained input');
    await click('Fade in');
    assert.deepEqual(await advance(1), [68]);
    await click('Fade out'); await click('Half');
    assert.equal((await frame()).value, 68, 'inputs do not advance the frame clock');
    assert.deepEqual(await advance(1), [66]);
    await advance(7);
    assert.equal((await frame()).value, 50); assert.equal((await frame()).remaining, 0);
    assert.equal(await name.evaluate(node => node.style.opacity), '0.5');
    assert(await page.evaluate(() => { const before = Module.webFrame; window.motionStep(); return before === Module.webFrame; }), 'settled content has no new paint');
    await click('Fade out'); await name.focus();
    await advance(8);
    assert.equal((await frame()).value, 0);
    assert.equal(await name.evaluate(node => node.style.opacity), '0');
    assert(await name.evaluate(node => document.activeElement === node));
    await name.fill('Kept while transparent'); await advance(1);
    await click('Add'); await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Count: 1')));
    await click('Fade in'); await advance(1);
    const pending = (await frame()).value;
    assert(pending > 0 && pending < 100);
    await page.evaluate(() => {
      window.motionPreferenceChange = undefined;
      Module.motionPreference.addEventListener('change', () => window.motionPreferenceChange = Module.motionPreference.matches, {once: true});
    });
    await page.emulateMedia({reducedMotion: 'reduce'});
    await page.evaluate(() => window.motionBrowserFrame());
    await page.waitForFunction(() => window.motionPreferenceChange === true);
    await advance(1);
    assert.equal((await frame()).value, 100); assert.equal((await frame()).remaining, 0);
    assert((await frame()).suppressed);
    await click('Fade out'); await advance(1);
    assert.equal((await frame()).value, 0); assert.equal((await frame()).remaining, 0);
    await page.evaluate(() => {
      window.motionPreferenceChange = undefined;
      Module.motionPreference.addEventListener('change', () => window.motionPreferenceChange = Module.motionPreference.matches, {once: true});
    });
    await page.emulateMedia({reducedMotion: 'no-preference'});
    await page.evaluate(() => window.motionBrowserFrame());
    await page.waitForFunction(() => window.motionPreferenceChange === false);
    await advance(1);
    await click('Fade in');
    assert.deepEqual(await advance(1), [13]);
    const show = page.getByRole('checkbox', {name: 'Show content', exact: true});
    await show.evaluate(node => node.click()); await advance(1);
    assert.equal(await name.count(), 0);
    assert.equal(await page.evaluate(() => Module.webFrame.transitions.length), 0);
    await click('Fade out'); await advance(1);
    await show.evaluate(node => node.click()); await advance(1);
    assert.equal((await frame()).value, 0); assert.equal((await frame()).remaining, 0);
    assert.equal(await name.inputValue(), 'Kept while transparent');
    await page.getByRole('checkbox', {name: 'Reduce motion', exact: true}).evaluate(node => node.click());
    await advance(1);
    await click('Fade in'); await advance(1);
    assert.equal((await frame()).value, 100); assert.equal((await frame()).remaining, 0);
    await name.focus(); await page.keyboard.press('Tab'); await page.keyboard.press('Enter'); await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Count: 2')));
    assert.deepEqual(errors, []);
    console.log(`web motion: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkSizeLimit(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 64, height: 64},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame?.transitions.some(item => item.kind === 'size'));
    const result = await page.evaluate(() => {
      const frame = Module.webFrame.transitions.find(item => item.kind === 'size');
      const canvas = Module.canvas, ctx = canvas.getContext('2d');
      return {size: [frame.width, frame.height, frame.targetWidth, frame.targetHeight, frame.remaining],
        first: Array.from(ctx.getImageData(0, 0, 1, 1).data),
        last: Array.from(ctx.getImageData(canvas.width - 1, canvas.height - 1, 1, 1).data)};
    });
    assert.deepEqual(result.size, [2147483647, 2147483647, 2147483647, 2147483647, 0]);
    assert.deepEqual(result.first, [255, 0, 0, 255]);
    assert.deepEqual(result.last, [255, 0, 0, 255]);
    assert.deepEqual(errors, []);
    console.log(`web size limit: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkPositionLimit(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 64, height: 96},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile, reducedMotion: 'reduce'});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame?.transitions.some(item => item.kind === 'position'));
    const state = () => page.evaluate(() => {
      const frame = Module.webFrame.transitions.find(item => item.kind === 'position');
      const scale = Module.webFrame.scale, ctx = Module.canvas.getContext('2d');
      const pixel = (x, y) => Array.from(ctx.getImageData(Math.floor(x * scale), Math.floor(y * scale), 1, 1).data);
      return {position: [frame.x, frame.y, frame.targetX, frame.targetY, frame.remaining],
        first: pixel(0, 0), inner: pixel(39, 29), outside: pixel(40, 30),
        source: Module.ccall('sz_web_snapshot', 'string', [], [])};
    });
    const high = await state();
    assert.deepEqual(high.position, [2147483647, 2147483647, 2147483647, 2147483647, 0]);
    assert.deepEqual(high.first, [255, 255, 255, 255]);
    assert.deepEqual(high.inner, [255, 255, 255, 255]);
    assert.equal((high.source.match(/9223372036854775807/g) || []).length, 2, 'display keeps both source values');
    assert(high.source.includes('text:X: 9223372036854775807') && high.source.includes('text:Y: 9223372036854775807'));
    await page.getByRole('button', {name: 'Minimum', exact: true}).click();
    await page.waitForFunction(() => Module.webFrame.transitions.some(item => item.kind === 'position' && item.x === 0 && item.y === 0 && item.remaining === 0));
    const low = await state();
    assert.deepEqual(low.position, [0, 0, 0, 0, 0]);
    assert.deepEqual(low.first, [255, 0, 0, 255]);
    assert.deepEqual(low.inner, [255, 0, 0, 255]);
    assert.deepEqual(low.outside, [255, 255, 255, 255]);
    assert.equal((low.source.match(/-9223372036854775808/g) || []).length, 2, 'zero coordinates keep both negative source values');
    assert(low.source.includes('text:X: -9223372036854775808') && low.source.includes('text:Y: -9223372036854775808'));
    assert.deepEqual(errors, []);
    console.log(`web position limit: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkSizeMotion(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 1400},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile, reducedMotion: 'no-preference'});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await openMotionPage(page, url);
    const state = () => page.evaluate(() => Module.webFrame.transitions.find(item => item.kind === 'size'));
    const advance = async count => {
      const frames = [];
      for (let i = 0; i < count; i++) {
        const deadline = Date.now() + 30000;
        while (!await page.evaluate(() => {
          const before = Module.webFrame; window.motionStep(); return before !== Module.webFrame;
        })) {
          assert(Date.now() < deadline, 'size transition paints a frame');
          await page.waitForTimeout(10);
        }
        frames.push(await state());
      }
      return frames;
    };
    const click = async name => page.getByRole('button', {name, exact: true}).evaluate(node => node.click());
    const show = page.getByRole('checkbox', {name: 'Show size content', exact: true});
    const group = page.getByRole('group', {name: 'Size bounds', exact: true});
    const name = page.getByRole('textbox', {name: 'Size name', exact: true});
    const after = page.getByRole('main', {name: 'Application', exact: true}).getByText('After size sample', {exact: true});
    await page.getByRole('checkbox', {name: 'Show size sample', exact: true}).evaluate(node => node.click());
    await advance(1);
    assert.deepEqual([(await state()).width, (await state()).height], [80, 60]);
    const firstBox = await group.boundingBox(), firstAfter = await after.boundingBox();
    const gap = firstAfter.y - firstBox.y - firstBox.height;
    const geometry = async (width, height) => {
      const box = await group.boundingBox(), below = await after.boundingBox();
      assert(Math.abs(box.width - width) < .01 && Math.abs(box.height - height) < .01,
        JSON.stringify({box, width, height}));
      assert(Math.abs(below.y - box.y - height - gap) < .01, 'the next label follows the current height');
      const pixels = await page.evaluate(box => {
        const scale = Module.webFrame.scale;
        const canvas = Module.canvas, ctx = canvas.getContext('2d');
        const pixel = (x, y) => Array.from(ctx.getImageData(Math.floor(x), Math.floor(y), 1, 1).data);
        return {inside: pixel((box.x + box.width - 2) * scale, (box.y + 2) * scale),
          outside: pixel((box.x + box.width + 2) * scale, (box.y + 2) * scale), paper: pixel(0, 0)};
      }, box);
      assert.deepEqual(pixels.inside, [225, 235, 245, 255]);
      assert.deepEqual(pixels.outside, pixels.paper);
    };
    await geometry(80, 60);
    const point = [firstBox.x + 4, firstBox.y + firstBox.height + 2];
    if (mobile) await page.touchscreen.tap(...point); else await page.mouse.click(...point);
    await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Size count: 0')));
    await name.fill('Kept size input');
    await name.evaluate(node => { node.setSelectionRange(2, 5); node.dispatchEvent(new Event('select')); });
    const original = await name.elementHandle();
    await click('Grow size');
    await name.evaluate(node => { node.blur(); node.focus(); });
    const grown = [];
    for (let i = 0; i < 3; i++) {
      const frame = (await advance(1))[0]; grown.push([frame.width, frame.height, frame.remaining]);
      await geometry(frame.width, frame.height);
    }
    assert.deepEqual(grown, [[100, 75, 7], [120, 90, 6], [140, 105, 5]]);
    assert(await name.evaluate(node => document.activeElement === node));
    assert.deepEqual(await name.evaluate(node => [node.value, node.selectionStart, node.selectionEnd]), ['Kept size input', 2, 5]);
    await click('Narrow size');
    const narrowed = await advance(8);
    assert.deepEqual(narrowed.map(frame => [frame.width, frame.height]),
      [[128, 96], [115, 86], [103, 77], [90, 68], [78, 58], [65, 49], [53, 39], [40, 30]]);
    await geometry(40, 30);
    assert(await original.evaluate(node => node === document.querySelector('input[aria-label="Size name"]')));
    await click('Wide size');
    const wide = await advance(8);
    assert(wide.every(frame => frame.height === 30));
    assert.equal(wide.at(-1).width, 240);
    await click('Tall size');
    const tall = await advance(8);
    assert(tall.every(frame => frame.width === 240));
    assert.equal(tall.at(-1).height, 180);
    await geometry(240, 180);
    await click('Size add'); await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Size count: 1')));
    await click('Collapse size'); await advance(8);
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [0, 0, 0]);
    assert.equal(await name.count(), 0); assert.equal(await group.count(), 0);
    assert.equal(await original.evaluate(node => node === document.activeElement), false);
    await click('Grow size'); await advance(8);
    assert.equal(await name.inputValue(), 'Kept size input');
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Size count: 1')));
    await click('Shrink size'); await advance(2);
    assert.deepEqual([(await state()).width, (await state()).height], [200, 150]);
    const frozen = await state();
    await page.waitForTimeout(100);
    assert.deepEqual(await state(), frozen, 'elapsed time does not advance a transition');
    await show.evaluate(node => node.click()); await advance(1);
    assert.equal(await state(), undefined); assert.equal(await name.count(), 0);
    await click('Grow size'); await advance(1);
    await show.evaluate(node => node.click()); await advance(1);
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [240, 180, 0]);
    assert.equal(await name.inputValue(), 'Kept size input');
    await click('Shrink size'); await advance(2);
    await page.evaluate(() => {
      window.motionPreferenceChange = undefined;
      Module.motionPreference.addEventListener('change', () => window.motionPreferenceChange = Module.motionPreference.matches, {once: true});
    });
    await page.emulateMedia({reducedMotion: 'reduce'});
    await page.evaluate(() => window.motionBrowserFrame());
    await page.waitForFunction(() => window.motionPreferenceChange === true);
    await advance(1);
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [80, 60, 0]);
    assert((await state()).suppressed);
    await click('Collapse size'); await advance(1);
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [0, 0, 0]);
    assert.equal(await name.count(), 0);
    assert.deepEqual(errors, []);
    console.log(`web size motion: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkContentMotion(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 2000},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile, reducedMotion: 'no-preference'});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await openMotionPage(page, url);
    const state = () => page.evaluate(() => Module.webFrame.transitions.find(item => item.kind === 'contentSize'));
    const advance = async count => {
      const frames = [];
      for (let i = 0; i < count; i++) {
        const deadline = Date.now() + 30000;
        while (!await page.evaluate(() => {
          const before = Module.webFrame; window.motionStep(); return before !== Module.webFrame;
        })) {
          assert(Date.now() < deadline, 'content-size transition paints a frame');
          await page.waitForTimeout(10);
        }
        frames.push(await state());
      }
      return frames;
    };
    const click = name => page.getByRole('button', {name, exact: true}).evaluate(node => node.click());
    const name = page.getByRole('textbox', {name: 'Content name', exact: true});
    const group = page.getByRole('group', {name: 'Content bounds', exact: true});
    const after = page.getByRole('main', {name: 'Application', exact: true}).getByText('After content sample', {exact: true});
    const show = page.getByRole('checkbox', {name: 'Show measured content', exact: true});
    await page.getByRole('checkbox', {name: 'Show content sample', exact: true}).evaluate(node => node.click());
    await advance(1);
    const width = mobile ? 366 : 616;
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [width, 100, 0]);
    const initial = await group.boundingBox(), below = await after.boundingBox();
    const gap = below.y - initial.y - 100;
    const geometry = async height => {
      const box = await group.boundingBox(), next = await after.boundingBox();
      assert(Math.abs(next.y - box.y - height - gap) < .01, 'following content uses the animated height');
      const pixels = await page.evaluate(({box, height}) => {
        const scale = Module.webFrame.scale, ctx = Module.canvas.getContext('2d');
        const pixel = (x, y) => Array.from(ctx.getImageData(Math.floor(x * scale), Math.floor(y * scale), 1, 1).data);
        return {inside: pixel(box.x + box.width - 2, box.y + height - 2),
          outside: pixel(box.x + box.width - 2, box.y + height + 2), paper: pixel(0, 0)};
      }, {box, height});
      assert.deepEqual(pixels.inside, [225, 235, 245, 255]);
      assert.deepEqual(pixels.outside, pixels.paper);
    };
    await geometry(100);
    await name.fill('Kept content input');
    await name.evaluate(node => { node.setSelectionRange(2, 5); node.dispatchEvent(new Event('select')); });
    const original = await name.elementHandle();
    await click('Grow content');
    const grown = [];
    for (let i = 0; i < 3; i++) {
      const frame = (await advance(1))[0]; grown.push([frame.width, frame.height, frame.targetHeight, frame.remaining]);
      await geometry(frame.height);
    }
    assert.deepEqual(grown, [[width, 107.5, 160, 7], [width, 115, 160, 6], [width, 122.5, 160, 5]]);
    assert(await name.evaluate(node => document.activeElement === node));
    assert.deepEqual(await name.evaluate(node => [node.value, node.selectionStart, node.selectionEnd]), ['Kept content input', 2, 5]);
    const button = await page.getByRole('button', {name: 'Content add', exact: true}).boundingBox();
    const point = [button.x + button.width / 2, initial.y + 145];
    if (mobile) await page.touchscreen.tap(...point); else await page.mouse.click(...point);
    await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Content count: 0')));
    await click('Detail content');
    const retarget = await advance(8);
    assert.deepEqual(retarget.map(frame => frame.height), [128.75, 127.5, 126.25, 125, 123.75, 122.5, 121.25, 120]);
    await geometry(120);
    assert(await original.evaluate(node => node === document.querySelector('input[aria-label="Content name"]')));
    assert.deepEqual(await name.evaluate(node => [node.value, node.selectionStart, node.selectionEnd]), ['Kept content input', 2, 5]);
    await click('Content add'); await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Content count: 1')));
    await click('Empty content'); await advance(8);
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [0, 0, 0]);
    assert.equal(await name.count(), 0); assert.equal(await group.count(), 0);
    assert.equal(await original.evaluate(node => node === document.activeElement), false);
    await click('Shrink content'); await advance(8);
    assert.equal(await name.inputValue(), 'Kept content input');
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Content count: 1')));
    await click('Grow content'); await advance(2);
    const frozen = await state();
    await page.waitForTimeout(100);
    assert.deepEqual(await state(), frozen, 'elapsed time does not advance measured size');
    await show.evaluate(node => node.click()); await advance(1);
    assert.equal(await state(), undefined); assert.equal(await name.count(), 0);
    await click('Detail content'); await advance(1);
    await show.evaluate(node => node.click()); await advance(1);
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [width, 120, 0]);
    assert.equal(await name.inputValue(), 'Kept content input');
    await click('Grow content'); await advance(2);
    await page.evaluate(() => {
      window.motionPreferenceChange = undefined;
      Module.motionPreference.addEventListener('change', () => window.motionPreferenceChange = Module.motionPreference.matches, {once: true});
    });
    await page.emulateMedia({reducedMotion: 'reduce'});
    await page.evaluate(() => window.motionBrowserFrame());
    await page.waitForFunction(() => window.motionPreferenceChange === true);
    await advance(1);
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [width, 160, 0]);
    assert((await state()).suppressed);
    await geometry(160);
    await click('Empty content'); await advance(1);
    assert.deepEqual([(await state()).width, (await state()).height, (await state()).remaining], [0, 0, 0]);
    assert.equal(await name.count(), 0);
    assert.deepEqual(errors, []);
    console.log(`web content-size motion: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkScaleLimit(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame?.transitions.some(item => item.kind === 'scale'));
    const state = () => page.evaluate(() => {
      const frame = Module.webFrame.transitions.find(item => item.kind === 'scale');
      return [frame.value, frame.target, frame.remaining];
    });
    const pixel = async (label, x, y) => {
      const box = await page.getByRole('group', {name: label, exact: true}).boundingBox();
      return page.evaluate(({box, x, y}) => {
        const scale = Module.webFrame.scale;
        return Array.from(Module.canvas.getContext('2d').getImageData(Math.floor((box.x + x) * scale), Math.floor((box.y + y) * scale), 1, 1).data);
      }, {box, x, y});
    };
    const raw = () => page.evaluate(() => Module.ccall('sz_web_snapshot', 'string', [], []));
    assert.deepEqual(await state(), [2147483647, 2147483647, 0]);
    assert((await raw()).includes('text:Raw scale: 9223372036854775807'));
    for (const label of ['Animated scale scene', 'Static scale scene']) {
      assert.deepEqual(await pixel(label, 1, 1), [255, 0, 0, 255]);
      assert.deepEqual(await pixel(label, 63, 31), [255, 0, 0, 255]);
    }
    assert.deepEqual(await pixel('Zero scale scene', 20, 15), [255, 255, 255, 255]);
    await page.getByRole('button', {name: 'Minimum scale', exact: true}).click();
    await page.waitForFunction(() => Module.webFrame.transitions.some(item => item.kind === 'scale' && item.value === 0 && item.remaining === 0));
    assert.deepEqual(await state(), [0, 0, 0]);
    assert((await raw()).includes('text:Raw scale: -9223372036854775808'));
    assert.deepEqual(await pixel('Animated scale scene', 20, 15), [255, 255, 255, 255]);
    await page.getByRole('button', {name: 'Normal scale', exact: true}).click();
    await page.waitForFunction(() => Module.webFrame.transitions.some(item => item.kind === 'scale' && item.value === 100 && item.remaining === 0));
    assert.deepEqual(await pixel('Animated scale scene', 39, 29), [255, 0, 0, 255]);
    assert.deepEqual(await pixel('Animated scale scene', 40, 30), [255, 255, 255, 255]);
    assert.deepEqual(errors, []);
    console.log(`web scale limits: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkScaleInput(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await openMotionPage(page, url);
    const advance = async count => {
      for (let i = 0; i < count; i++) {
        const deadline = Date.now() + 30000;
        while (!await page.evaluate(() => { const before = Module.webFrame; window.motionStep(); return before !== Module.webFrame; })) {
          assert(Date.now() < deadline, 'scale input paints a frame');
          await page.waitForTimeout(10);
        }
      }
    };
    const pointer = async (x, y) => {
      if (mobile) await page.touchscreen.tap(x, y); else await page.mouse.click(x, y);
    };
    const scene = await page.getByRole('group', {name: 'Scale input scene', exact: true}).boundingBox();
    const field = page.getByRole('textbox', {name: 'Scaled field', exact: true});
    const choices = page.getByRole('combobox', {name: 'Scaled choices', exact: true});
    const action = page.getByRole('button', {name: 'Scaled action', exact: true});
    let box = await action.boundingBox();
    assert(Math.abs(box.x - scene.x - 40) < .01 && Math.abs(box.y - scene.y - 160) < .01);
    assert(Math.abs(box.width - 240) < .01 && Math.abs(box.height - 80) < .01);
    await action.focus(); await advance(1);
    const tooltip = page.getByRole('tooltip').filter({hasText: 'Scaled action help'});
    const help = await tooltip.boundingBox();
    assert(help && Math.abs(help.x - box.x) < .01 && Math.abs(help.y - box.y - box.height) < .01, JSON.stringify({help, box}));
    const trigger = await choices.boundingBox();
    await pointer(trigger.x + trigger.width / 2, trigger.y + trigger.height / 2); await advance(1);
    const list = page.getByRole('listbox', {name: 'Scaled choices', exact: true});
    const popup = await list.boundingBox();
    assert(popup && Math.abs(popup.x - trigger.x) < .01 && Math.abs(popup.y - trigger.y - trigger.height) < .01);
    assert(Math.abs(popup.width - 240) < .01 && Math.abs(popup.height - 120) < .01, JSON.stringify(popup));
    const option = page.getByRole('option', {name: 'Third', exact: true});
    box = await option.boundingBox();
    assert(Math.abs(box.y - popup.y - 80) < .01 && Math.abs(box.height - 40) < .01);
    assert(box.y >= scene.y + scene.height, 'the popup extends past the clipped scene');
    await pointer(box.x + box.width / 2, box.y + box.height / 2); await advance(1);
    assert.equal(await choices.getAttribute('aria-expanded'), 'false');
    assert.equal(await choices.textContent(), 'Third');
    box = await action.boundingBox();
    await pointer(box.x + box.width / 2, box.y + box.height / 2); await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(item => item.text === 'Action count: 1')));
    await page.getByRole('button', {name: 'Large scale', exact: true}).evaluate(node => node.click()); await advance(8);
    box = await field.boundingBox();
    assert(Math.abs(box.x - scene.x + 80) < .01 && Math.abs(box.y - scene.y + 120) < .01);
    assert(Math.abs(box.width - 480) < .01 && Math.abs(box.height - 160) < .01);
    assert(await field.evaluate((node, point) => document.elementFromPoint(point.x, point.y) !== node,
      {x: scene.x - 4, y: scene.y + 12}), 'the ancestor clip excludes the displayed field outside the scene');
    await pointer(scene.x + 8, scene.y + 12); await advance(1);
    assert(await field.evaluate(node => document.activeElement === node));
    assert.deepEqual(errors, []);
    console.log(`web scale input: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkScaleMotion(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 2000},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile, reducedMotion: 'no-preference'});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await openMotionPage(page, url);
    const state = () => page.evaluate(() => Module.webFrame.transitions.find(item => item.kind === 'scale'));
    const advance = async count => {
      const frames = [];
      for (let i = 0; i < count; i++) {
        const deadline = Date.now() + 30000;
        while (!await page.evaluate(() => {
          const before = Module.webFrame; window.motionStep(); return before !== Module.webFrame;
        })) {
          assert(Date.now() < deadline, 'scale transition paints a frame');
          await page.waitForTimeout(10);
        }
        frames.push(await state());
      }
      return frames;
    };
    const click = name => page.getByRole('button', {name, exact: true}).evaluate(node => node.click());
    const pointer = async (x, y) => {
      if (mobile) await page.touchscreen.tap(x, y); else await page.mouse.click(x, y);
    };
    const name = page.getByRole('textbox', {name: 'Scale name', exact: true});
    const group = page.getByRole('group', {name: 'Scale bounds', exact: true});
    const scene = page.getByRole('group', {name: 'Scale scene', exact: true});
    const add = page.getByRole('button', {name: 'Scale add', exact: true});
    const after = page.getByRole('main', {name: 'Application', exact: true}).getByText('After scale sample', {exact: true});
    const show = page.getByRole('checkbox', {name: 'Show scale content', exact: true});
    await page.getByRole('checkbox', {name: 'Show scale sample', exact: true}).evaluate(node => node.click());
    await advance(1);
    assert.deepEqual([(await state()).value, (await state()).remaining], [100, 0]);
    const origin = await scene.boundingBox(), below = await after.boundingBox(), firstButton = await add.boundingBox();
    const geometry = async percent => {
      const factor = percent / 100, box = await group.boundingBox(), field = await name.boundingBox();
      const x = origin.x + 160 - 60 * factor, y = origin.y + 120 - 40 * factor;
      assert(Math.abs(box.x - x) < .02 && Math.abs(box.y - y) < .02, JSON.stringify({box, x, y}));
      assert(Math.abs(box.width - 120 * factor) < .02 && Math.abs(box.height - 80 * factor) < .02);
      assert(Math.abs(field.x - x) < .02 && Math.abs(field.y - y) < .02);
      assert(Math.abs(field.width - 120 * factor) < .02 && Math.abs(field.height - 40 * factor) < .02);
      assert(Math.abs((await after.boundingBox()).y - below.y) < .01, 'scale preserves surrounding layout');
      const pixel = await page.evaluate(({x, y}) => {
        const scale = Module.webFrame.scale;
        return Array.from(Module.canvas.getContext('2d').getImageData(Math.floor(x * scale), Math.floor(y * scale), 1, 1).data);
      }, {x: x + 118 * factor, y: y + 78 * factor});
      assert.deepEqual(pixel, [225, 235, 245, 255]);
    };
    await geometry(100);
    await name.fill('Kept scale input');
    await name.evaluate(node => { node.setSelectionRange(2, 5); node.dispatchEvent(new Event('select')); });
    const original = await name.elementHandle();
    await click('Grow scale');
    const grow = [];
    for (let i = 0; i < 3; i++) {
      const frame = (await advance(1))[0]; grow.push([frame.value, frame.remaining]);
      await geometry(frame.value);
    }
    assert.deepEqual(grow, [[113, 7], [125, 6], [138, 5]]);
    assert(await name.evaluate(node => document.activeElement === node));
    assert.deepEqual(await name.evaluate(node => [node.value, node.selectionStart, node.selectionEnd]), ['Kept scale input', 2, 5]);
    await click('Shrink scale');
    const shrink = await advance(8);
    assert.deepEqual(shrink.map(frame => frame.value), [127, 116, 105, 94, 83, 72, 61, 50]);
    await geometry(50);
    assert(await original.evaluate(node => node === document.querySelector('input[aria-label="Scale name"]')));
    await pointer(firstButton.x + firstButton.width - 6, firstButton.y + firstButton.height - 6);
    await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Scale count: 0')));
    const current = await add.boundingBox();
    await pointer(current.x + current.width / 2, current.y + current.height / 2); await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Scale count: 1')));
    await click('Collapse scale'); await advance(8);
    assert.deepEqual([(await state()).value, (await state()).remaining], [0, 0]);
    assert.equal(await name.count(), 0); assert.equal(await add.count(), 0);
    await click('Reset scale'); await advance(8); await geometry(100);
    assert.equal(await name.inputValue(), 'Kept scale input');
    await click('Grow scale'); await advance(2);
    const frozen = await state(); await page.waitForTimeout(100);
    assert.deepEqual(await state(), frozen, 'elapsed time does not advance scale');
    await show.evaluate(node => node.click()); await advance(1);
    assert.equal(await state(), undefined); assert.equal(await name.count(), 0);
    await click('Grow scale'); await advance(1);
    await show.evaluate(node => node.click()); await advance(1);
    assert.deepEqual([(await state()).value, (await state()).remaining], [200, 0]);
    await geometry(200); assert.equal(await name.inputValue(), 'Kept scale input');
    await click('Shrink scale'); await advance(2);
    await page.evaluate(() => {
      window.motionPreferenceChange = undefined;
      Module.motionPreference.addEventListener('change', () => window.motionPreferenceChange = Module.motionPreference.matches, {once: true});
    });
    await page.emulateMedia({reducedMotion: 'reduce'});
    await page.evaluate(() => window.motionBrowserFrame());
    await page.waitForFunction(() => window.motionPreferenceChange === true);
    await advance(1);
    assert.deepEqual([(await state()).value, (await state()).remaining], [50, 0]);
    assert((await state()).suppressed); await geometry(50);
    assert.deepEqual(errors, []);
    console.log(`web scale motion: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkPositionMotion(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 2000},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile, reducedMotion: 'no-preference'});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await openMotionPage(page, url);
    const state = () => page.evaluate(() => Module.webFrame.transitions.find(item => item.kind === 'position'));
    const advance = async count => {
      const frames = [];
      for (let i = 0; i < count; i++) {
        const deadline = Date.now() + 30000;
        while (!await page.evaluate(() => {
          const before = Module.webFrame; window.motionStep(); return before !== Module.webFrame;
        })) {
          assert(Date.now() < deadline, 'position transition paints a frame');
          await page.waitForTimeout(10);
        }
        frames.push(await state());
      }
      return frames;
    };
    const click = name => page.getByRole('button', {name, exact: true}).evaluate(node => node.click());
    const pointer = async (x, y) => {
      if (mobile) await page.touchscreen.tap(x, y); else await page.mouse.click(x, y);
    };
    const name = page.getByRole('textbox', {name: 'Position name', exact: true});
    const group = page.getByRole('group', {name: 'Position bounds', exact: true});
    const scene = page.getByRole('group', {name: 'Position scene', exact: true});
    const add = page.getByRole('button', {name: 'Position add', exact: true});
    const after = page.getByRole('main', {name: 'Application', exact: true}).getByText('After position sample', {exact: true});
    const show = page.getByRole('checkbox', {name: 'Show position content', exact: true});
    await page.getByRole('checkbox', {name: 'Show position sample', exact: true}).evaluate(node => node.click());
    await advance(1);
    assert.deepEqual([(await state()).x, (await state()).y, (await state()).remaining], [16, 16, 0]);
    const origin = await scene.boundingBox(), below = await after.boundingBox();
    const firstButton = await add.boundingBox();
    const geometry = async (x, y, oldIsEmpty = false) => {
      const box = await group.boundingBox(), field = await name.boundingBox(), next = await after.boundingBox();
      assert(Math.abs(box.x - origin.x - x) < .01 && Math.abs(box.y - origin.y - y) < .01, JSON.stringify({box, origin, x, y}));
      assert(Math.abs(box.width - 120) < .01 && Math.abs(box.height - 80) < .01);
      assert(Math.abs(field.x - box.x) < .01 && Math.abs(field.y - box.y) < .01 && Math.abs(field.width - 120) < .01 && Math.abs(field.height - 40) < .01);
      assert(Math.abs(next.y - below.y) < .01, 'the bounded scene keeps following content in place');
      const pixels = await page.evaluate(({box, origin}) => {
        const scale = Module.webFrame.scale, ctx = Module.canvas.getContext('2d');
        const pixel = (x, y) => Array.from(ctx.getImageData(Math.floor(x * scale), Math.floor(y * scale), 1, 1).data);
        return {inside: pixel(box.x + 118, box.y + 78), old: pixel(origin.x + 18, origin.y + 94)};
      }, {box, origin});
      assert.deepEqual(pixels.inside, [225, 235, 245, 255]);
      if (oldIsEmpty) assert.deepEqual(pixels.old, [255, 255, 255, 255]);
    };
    await geometry(16, 16);
    await name.fill('Kept position input');
    await name.evaluate(node => { node.setSelectionRange(2, 5); node.dispatchEvent(new Event('select')); });
    const original = await name.elementHandle();
    await click('Far position');
    const moved = [];
    for (let i = 0; i < 3; i++) {
      const frame = (await advance(1))[0]; moved.push([frame.x, frame.y, frame.remaining]);
      await geometry(frame.x, frame.y, true);
    }
    assert.deepEqual(moved, [[36, 31, 7], [56, 46, 6], [76, 61, 5]]);
    assert(await name.evaluate(node => document.activeElement === node));
    assert.deepEqual(await name.evaluate(node => [node.value, node.selectionStart, node.selectionEnd]), ['Kept position input', 2, 5]);
    await pointer(firstButton.x + firstButton.width / 2, firstButton.y + firstButton.height / 2);
    await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Position count: 0')));
    await click('Near position');
    const retarget = await advance(8);
    assert.deepEqual(retarget.map(frame => [frame.x, frame.y]),
      [[91, 70], [86, 64], [81, 57], [76, 51], [71, 45], [66, 39], [61, 32], [56, 26]]);
    await geometry(56, 26);
    assert(await original.evaluate(node => node === document.querySelector('input[aria-label="Position name"]')));
    assert.equal(await name.inputValue(), 'Kept position input');
    const current = await add.boundingBox();
    await pointer(current.x + current.width / 2, current.y + current.height / 2); await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Position count: 1')));
    await click('Home position'); await advance(8); await geometry(0, 0);
    assert.equal(await name.count(), 1);
    await click('Right position');
    const right = await advance(8); assert(right.every(frame => frame.y === 0));
    assert.equal(right.at(-1).x, 176);
    await click('Down position');
    const down = await advance(8); assert(down.every(frame => frame.x === 176));
    assert.equal(down.at(-1).y, 136); await geometry(176, 136);
    await click('Near position'); await advance(2);
    const frozen = await state(); await page.waitForTimeout(100);
    assert.deepEqual(await state(), frozen, 'elapsed time does not advance a position transition');
    await show.evaluate(node => node.click()); await advance(1);
    assert.equal(await state(), undefined); assert.equal(await name.count(), 0);
    await click('Far position'); await advance(1);
    await show.evaluate(node => node.click()); await advance(1);
    assert.deepEqual([(await state()).x, (await state()).y, (await state()).remaining], [176, 136, 0]);
    assert.equal(await name.inputValue(), 'Kept position input');
    await click('Near position'); await advance(2);
    await page.evaluate(() => {
      window.motionPreferenceChange = undefined;
      Module.motionPreference.addEventListener('change', () => window.motionPreferenceChange = Module.motionPreference.matches, {once: true});
    });
    await page.emulateMedia({reducedMotion: 'reduce'});
    await page.evaluate(() => window.motionBrowserFrame());
    await page.waitForFunction(() => window.motionPreferenceChange === true);
    await advance(1);
    assert.deepEqual([(await state()).x, (await state()).y, (await state()).remaining], [56, 26, 0]);
    assert((await state()).suppressed); await geometry(56, 26);
    await click('Outside position'); await advance(1);
    assert.deepEqual([(await state()).x, (await state()).y, (await state()).remaining], [330, 250, 0]);
    await pointer(origin.x + 331, origin.y + 251); await advance(1);
    assert(await page.evaluate(() => Module.textBlocks.some(block => block.text === 'Position count: 1')));
    await click('Home position'); await advance(1); await geometry(0, 0);
    assert.equal(await name.inputValue(), 'Kept position input');
    assert.deepEqual(errors, []);
    console.log(`web position motion: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkSelect(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage(), errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame.items.some(item => item.role === 55));
    const language = page.getByRole('combobox', {name: 'Language', exact: true});
    const items = page.getByRole('combobox', {name: 'Items', exact: true});
    const empty = page.getByRole('combobox', {name: 'Empty', exact: true});
    const name = page.getByRole('textbox', {name: 'Name', exact: true});
    const text = value => page.waitForFunction(value => Module.textBlocks.some(block => block.text === value), value);
    const expanded = async (control, open) => {
      await page.waitForFunction(({node, open}) => node.getAttribute('aria-expanded') === String(open), {node: await control.elementHandle(), open});
    };
    const active = async control => {
      const id = await control.getAttribute('aria-activedescendant');
      assert(id, 'open combobox has an active option');
      return page.locator('[id="' + id + '"]');
    };
    const expectActive = async (control, label) => {
      try {
        await page.waitForFunction(({node, label}) => document.getElementById(node.getAttribute('aria-activedescendant'))?.textContent === label,
          {node: await control.elementHandle(), label});
      } catch (error) {
        console.error(await control.evaluate(node => ({control: node.outerHTML, focused: document.activeElement?.outerHTML,
          items: Module.webFrame.items.filter(item => item.role === 55), snapshot: Module.ccall('sz_web_snapshot', 'string', [], [])})));
        throw error;
      }
    };
    assert.equal(await language.getAttribute('aria-expanded'), 'false');
    assert.equal(await language.getAttribute('aria-haspopup'), 'listbox');
    const popupId = await language.getAttribute('aria-controls');
    assert.equal(await page.locator('[id="' + popupId + '"]').getAttribute('role'), 'listbox');
    assert.equal(await empty.getAttribute('aria-disabled'), 'true');
    assert.equal(await empty.getAttribute('tabindex'), '-1');
    await language.focus();
    await page.keyboard.press('ArrowDown');
    await expanded(language, true);
    await expectActive(language, 'Alpha');
    assert.equal(await page.getByRole('option').count(), 5);
    await page.keyboard.press('ArrowDown');
    await page.waitForFunction(node => document.getElementById(node.getAttribute('aria-activedescendant'))?.textContent === 'Alpine', await language.elementHandle());
    assert(await language.evaluate(node => document.activeElement === node), 'DOM focus stays on the combobox');
    await text('Choice: -1');
    await page.keyboard.press('Escape'); await expanded(language, false); await text('Choice: -1');
    await page.keyboard.press('End'); await expanded(language, true);
    await expectActive(language, '中文');
    await page.keyboard.press('Enter'); await expanded(language, false); await text('Choice: 4');
    await page.keyboard.press('B'); await expanded(language, true);
    await page.keyboard.press('r');
    await expectActive(language, 'Bravo');
    await text('Choice: 4');
    await page.keyboard.press('Escape'); await expanded(language, false);
    await page.keyboard.press('B');
    await page.waitForTimeout(1100);
    await page.keyboard.press('r');
    await expectActive(language, 'Beta');
    await page.keyboard.press('Enter'); await text('Choice: 2');
    assert.equal(await language.textContent(), 'Beta');
    await page.keyboard.press('Home'); await expanded(language, true);
    if (mobile) await page.getByRole('option', {name: 'Bravo', exact: true}).tap();
    else await page.getByRole('option', {name: 'Bravo', exact: true}).click();
    await expanded(language, false); await text('Choice: 3');
    await language.focus(); await page.keyboard.press('End'); await expanded(language, true);
    const add = page.getByRole('button', {name: 'Add', exact: true});
    if (mobile) await add.tap(); else await add.click();
    await expanded(language, false); await text('Choice: 3'); await text('Count: 0');
    if (mobile) await add.tap(); else await add.click();
    await text('Count: 1');
    assert.equal(await name.inputValue(), 'Keep this text');
    await language.focus(); await page.keyboard.press('Tab');
    assert(await items.evaluate(node => document.activeElement === node));
    await page.keyboard.press('End'); await expanded(items, true);
    const last = await active(items);
    assert.equal(await last.textContent(), 'Item129');
    assert.equal(await last.getAttribute('aria-posinset'), '130');
    assert.equal(await last.getAttribute('aria-setsize'), '130');
    assert.equal(await last.getAttribute('aria-selected'), 'true');
    assert(await page.getByRole('option').count() <= 9, 'large options use a bounded DOM window');
    await text('Item: 0');
    await page.keyboard.press('PageUp');
    await expectActive(items, 'Item119');
    await page.keyboard.press('Enter'); await expanded(items, false); await text('Item: 119');
    await page.keyboard.press('Home'); await expanded(items, true);
    const popup = page.getByRole('listbox', {name: 'Items', exact: true});
    const box = await popup.boundingBox();
    if (mobile) {
      await page.evaluate(({x, y}) => {
        const send = (type, py) => {
          const event = new Event(type, {bubbles: true, cancelable: true});
          Object.defineProperties(event, {pointerId: {value: 77}, pointerType: {value: 'touch'}, isPrimary: {value: true},
            button: {value: 0}, clientX: {value: x}, clientY: {value: py}});
          document.dispatchEvent(event);
        };
        send('pointerdown', y); send('pointermove', y - 120); send('pointerup', y - 120);
      }, {x: box.x + box.width / 2, y: box.y + box.height - 20});
    } else {
      await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
      await page.mouse.wheel(0, 600);
    }
    await page.waitForFunction(node => {
      const option = document.getElementById(node.getAttribute('aria-activedescendant'));
      return option && Number(option.getAttribute('aria-posinset')) > 1;
    }, await items.elementHandle());
    assert(await page.getByRole('option').count() <= 9);
    await text('Item: 119');
    await page.keyboard.press('Escape'); await expanded(items, false);
    await page.keyboard.press('Tab');
    assert(await name.evaluate(node => document.activeElement === node), 'Tab skips the empty select');
    const settings = page.getByRole('button', {name: 'Settings', exact: true});
    if (mobile) await settings.tap(); else await settings.click();
    const dialog = page.getByRole('dialog', {name: 'Settings', exact: true});
    await dialog.waitFor();
    const choice = dialog.getByRole('combobox', {name: 'Dialog choice', exact: true});
    await choice.focus(); await page.keyboard.press('Home'); await expanded(choice, true);
    assert(await dialog.getByRole('listbox', {name: 'Dialog choice', exact: true}).isVisible(), 'popup stays in the dialog accessibility scope');
    const bounds = await dialog.getByRole('listbox').boundingBox();
    assert(bounds.y >= 0 && bounds.y + bounds.height <= 720, 'popup fits the viewport');
    await page.keyboard.press('Escape'); await expanded(choice, false);
    assert(await dialog.isVisible());
    await page.keyboard.press('Escape'); await dialog.waitFor({state: 'hidden'});
    assert(await settings.evaluate(node => document.activeElement === node), 'dialog focus returns to its opener');
    await items.focus(); await page.keyboard.press('End'); await expanded(items, true);
    const id = await items.getAttribute('id');
    await page.setViewportSize({width: mobile ? 320 : 280, height: 520});
    await expanded(items, true);
    assert.equal(await items.getAttribute('id'), id);
    assert(await items.evaluate(node => document.activeElement === node));
    const resized = await page.getByRole('listbox', {name: 'Items', exact: true}).boundingBox();
    assert(resized.y >= 0 && resized.y + resized.height <= 520);
    await page.keyboard.press('Enter'); await text('Item: 129');
    assert.equal(await name.inputValue(), 'Keep this text');
    await page.setViewportSize({width: mobile ? 390 : 640, height: 720});
    const controlId = await language.getAttribute('id');
    const fieldNode = await name.elementHandle();
    await language.focus(); await page.keyboard.press('End');
    await expectActive(language, '中文');
    const oldOptionId = await (await active(language)).getAttribute('id');
    await page.keyboard.press('Enter'); await text('Choice: 4');
    const press = async label => {
      const button = page.getByRole('button', {name: label, exact: true});
      if (mobile) await button.tap(); else await button.click();
    };
    await press('Replace options');
    await page.waitForFunction(node => node.textContent === '', await language.elementHandle());
    await text('Choice: 4');
    assert.equal(await language.getAttribute('id'), controlId);
    await language.focus(); await page.keyboard.press('Home');
    await expectActive(language, 'Gamma');
    assert.equal(await page.getByRole('option').count(), 4);
    assert.equal(await (await active(language)).getAttribute('aria-setsize'), '4');
    assert.notEqual(await (await active(language)).getAttribute('id'), oldOptionId);
    await page.keyboard.press('End'); await expectActive(language, 'Same');
    assert.equal(await (await active(language)).getAttribute('aria-posinset'), '4');
    await page.keyboard.press('Enter'); await text('Choice: 3');
    assert.equal(await language.textContent(), 'Same');
    await press('Clear options'); await text('Choice: 3');
    await page.waitForFunction(node => node.getAttribute('aria-disabled') === 'true', await language.elementHandle());
    assert.equal(await language.getAttribute('tabindex'), '-1');
    assert.equal(await language.getAttribute('aria-expanded'), 'false');
    assert.equal(await language.getAttribute('aria-activedescendant'), null);
    assert.equal(await page.getByRole('option').count(), 0);
    await press('Restore options');
    await page.waitForFunction(node => node.textContent === 'Bravo' && node.getAttribute('aria-disabled') === 'false', await language.elementHandle());
    assert.equal(await language.getAttribute('id'), controlId);
    await language.focus(); await page.keyboard.press('Home');
    await expectActive(language, 'Alpha');
    await page.keyboard.press('Enter'); await text('Choice: 0');
    assert(await fieldNode.evaluate(node => node === document.querySelector('input[aria-label="Name"]')));
    assert.equal(await name.inputValue(), 'Keep this text');
    assert.deepEqual(errors, []);
    console.log(`web select: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function activateInline(page, node, mobile) {
  const point = await node.evaluate(node => {
    const box = node.item.boxes[0], scale = Module.webFrame.scale;
    const x = (box[0] + box[2] / 2) / scale, y = (box[1] + box[3] / 2) / scale;
    return {x, y, hit: node.contains(document.elementFromPoint(x, y))};
  });
  assert(point.hit, 'The action fragment receives pointer input');
  if (mobile) await page.touchscreen.tap(point.x, point.y); else await page.mouse.click(point.x, point.y);
}

async function checkShapingFontChanges(page, browserType, mobile, joining = false) {
  await page.setViewportSize({width: mobile ? 390 : 640, height: joining ? 2000 : 1600});
  await page.waitForFunction(() => Module.canvas.height === Math.round(innerHeight * devicePixelRatio));
  const panel = page.getByRole('button', {name: joining ? 'Joining' : 'Font changes', exact: true});
  await panel.click();
  await page.waitForFunction(source => Module.textBlocks.some(block => block.text === source), joining ? 'مر' : 'אבגדהו');
  const cases = joining ? [
    ['مر', [['ﺮ', 18, 0, true], ['ﻣ', 18, 1, true]], false, false, [18, 1]],
    ['مر', [['ﺮ', 18, 0, true], ['ﻣ', 18, 2, true]], false, false, [18, 2]],
    ['مر', [['ﺮ', 18, 0, true], ['ﻣ', 24, 0, true]], false, false, [24, 0]],
    ['لا', [['ﺎ', 18, 0, true], ['ﻟ', 18, 1, true]]],
    ['در', [['ﺭ', 18, 0, true], ['ﺩ', 18, 1, true]]],
    ['م ر', [['ﺭ', 18, 0, true], ['ﻡ ', 18, 1, true]]],
    // Keep the reference mark visible after a presentation form in WebKit.
    ['مَر', [['ﺮ', 18, 0, true], ['ﻣَ\u034f', 18, 1, true]]],
    ['م\u200cر', [['ﺭ', 18, 0, true], ['ﻡ', 18, 1, true]]],
    ['د\u200dر', [['ﺮ', 18, 0, true], ['ﺩ', 18, 1, true]]],
    ['م\nر', [['ﺭ', 18, 0, true]], true],
    ['مر', [['ﺭ', 18, 0, true]], true, false, [18, 1]],
    ['ممر', [['ﺮ', 14, 0, true], ['ﻤ', 18, 2, true], ['ﻣ', 24, 1, true]], false, false, [24, 1]],
    ['Latin\nمر', [['ﺮ', 18, 0, true], ['ﻣ', 18, 1, true]], true],
    ['\u202dمر\u202c', [['ﻡ', 18, 1, false], ['ﺭ', 18, 0, false]]],
    ['\u202dمم\u202c', [['ﻢ', 18, 1, false], ['ﻣ', 18, 0, false]]],
    ['\u202eAمر\u202c', [['ﺮ', 18, 0, true], ['\u202eAﻣ\u202c', 18, 1, true]]],
    ['م\u202eر\u202c', [['ﺭ', 18, 0, true], ['ﻡ', 18, 1, true]]],
    ['م\u2067ر\u2069', [['ﺭ', 18, 0, true], ['ﻡ', 18, 1, true]]],
    ['\u202dمعم\u202c', [['\u202dﻢﻌ\u202c', 18, 1, false], ['ﻣ', 18, 0, false]]],
    ['\u202dfiمم\u202c', [['fiﻢ', 18, 1, false], ['ﻣ', 18, 0, false]]],
    ['م'.repeat(10) + 'ر', null, false, true],
    ['م'.repeat(10) + 'د', null, false, true],
    ['م\u2067\u2069ر', [['ﺭ', 18, 0, true], ['ﻡ', 18, 0, true]], false, false, [18, 0]],
    ['م\u2067\u2069ر', [['ﺭ', 18, 0, true], ['ﻡ', 18, 1, true]]]
  ] : [
    ['אבגדהו', [['דהו', 18, 0, true], ['אבג', 24, 0, true]]],
    ['זחטיכל', [['יכל', 18, 0, true], ['זחט', 18, 1, true]]],
    ['מנסעפצ', [['עפצ', 18, 0, true], ['מנס', 18, 2, true]]],
    ['אב AB CD', [['AB', 24, 0, false], [' CD', 18, 0, false], ['אב ', 24, 0, true]]],
    ['ab אב גד', [['ab ', 24, 0, false], [' גד', 18, 0, true], ['אב', 24, 0, true]]],
    ['A\nאבגדהו', [['גדהו', 18, 0, true], ['אב', 24, 0, true]], true],
    ['אבגדהוז אבגדהו', [['גדהו', 18, 0, true], ['אב', 24, 0, true]], true],
    ['אבגדהוזחטיכלמנסע', null, false, true]
  ];
  for (const [source, pieces, wrapped, ellipsis, first] of cases) {
    const result = await page.evaluate(({source, pieces, wrapped, ellipsis, first, joining}) => {
      const scale = Module.webFrame.scale;
      const block = Module.textBlocks.find(block => block.text === source && !block.lines.some(line => line.action) &&
        (!first || block.lines[0].font === first[0] * scale && (block.lines[0].style & 3) === first[1] &&
          new Set(block.lines.map(line => line.y)).size === (wrapped ? 2 : 1)));
      if (!block) throw new Error('font case is absent: ' + source);
      const node = Module.nodes.get(block.id);
      const rows = [...new Set(block.lines.map(line => line.y))];
      const row = block.lines.filter(line => line.y === rows.at(-1));
      const end = Math.max(...row.map(line => line.end));
      const referencePieces = pieces || (joining ? (end === 1 ? [['ﻡ...', 18, 1, true]] :
        [['ﻤ'.repeat(end - 2) + 'ﻢ...', 18, 0, true], ['ﻣ', 18, 1, true]]) :
        [[source.slice(2, end) + '...', 18, 0, true], ['אב', 24, 0, true]]);
      const x = Math.min(...row.map(line => line.x)), right = Math.max(...row.map(line => line.x + line.width));
      const font = Math.max(...row.map(line => line.font)), baseline = row[0].y;
      const left = Math.floor(x) - 2, top = Math.floor(baseline - font);
      const width = Math.ceil(right) - left + 2, height = Math.ceil(font * 1.4);
      if (top < 0 || top + height > Module.canvas.height)
        throw new Error('The text row is outside the pixel check viewport: ' + source + ' at ' + top);
      const actual = Module.canvas.getContext('2d').getImageData(left, top, width, height).data;
      const background = Module.canvas.getContext('2d').getImageData(Module.canvas.width - 2, 2, 1, 1).data;
      const reference = document.createElement('canvas'); reference.width = width; reference.height = height;
      const ctx = reference.getContext('2d'); ctx.setTransform(scale, 0, 0, scale, 0, 0); ctx.textAlign = 'left'; ctx.fillStyle = '#ffffff';
      let next = x - left;
      for (const [text, size, style, rtl] of referencePieces) {
        ctx.font = ((style & 2) ? 'italic ' : '') + ((style & 1) ? 'bold ' : '') + size + 'px ' + Module.scuzzFontStack;
        ctx.direction = rtl ? 'rtl' : 'ltr';
        const marked = (rtl ? '\u200f' : '\u200e') + text;
        ctx.fillText(marked, (Math.fround(next + left) - left) / scale, (baseline - top) / scale); next += ctx.measureText(marked).width * scale;
      }
      const mask = ctx.getImageData(0, 0, width, height).data, ink = row[0].ink;
      const rgb = [(ink >>> 16) & 255, (ink >>> 8) & 255, ink & 255];
      let delta = 0;
      for (let p = 0; p < width * height; p++) for (let channel = 0; channel < 3; channel++) {
        const expected = Math.floor((rgb[channel] * mask[p * 4 + 3] + background[channel] * (255 - mask[p * 4 + 3])) / 255);
        delta = Math.max(delta, Math.abs(actual[p * 4 + channel] - expected));
      }
      const geometry = node.fragments.map((span, i) => {
        const box = span.getBoundingClientRect(), line = block.lines[i];
        return Math.max(Math.abs(box.left * scale - line.x), Math.abs(box.width * scale - line.width));
      });
      const range = document.createRange(); range.setStart(node.fragments[0].firstChild, 0);
      range.setEnd(node.fragments.at(-1).firstChild, node.fragments.at(-1).textContent.length);
      getSelection().removeAllRanges(); getSelection().addRange(range);
      let copied;
      const event = new Event('copy', {cancelable: true});
      Object.defineProperty(event, 'clipboardData', {value: {setData(type, value) { if (type === 'text/plain') copied = value; }}});
      document.dispatchEvent(event); getSelection().removeAllRanges();
      return {delta, geometry, rows: rows.length, copied, end, advance: row.reduce((sum, line) => sum + line.width, 0), expectedAdvance: next - (x - left),
        dotsLeft: !ellipsis || row.find(line => line.synthetic).x === x};
    }, {source, pieces, wrapped, ellipsis, joining, first: first || (joining ? [18, 1] : undefined)});
    assert(result.delta <= 2, source + ': ' + JSON.stringify(result));
    assert(result.geometry.every(delta => delta < 1), source + ': ' + JSON.stringify(result));
    if (joining && ellipsis) {
      assert(source.endsWith('ر') ? result.end === 1 : result.end >= 2, JSON.stringify(result));
      assert(result.end < source.length, source);
    }
    assert.equal(result.rows, wrapped ? 2 : 1, source);
    assert(result.dotsLeft && Math.abs(result.advance - result.expectedAdvance) < .1, source + ': ' + JSON.stringify(result));
    assert.equal(result.copied, ellipsis ? source.slice(0, result.end) : source);
  }
  const action = page.getByRole('button', {name: joining ? 'محمد' : 'קר AB CD', exact: true});
  await action.evaluate(node => node.focus({preventScroll: true}));
  await activateInline(page, action, mobile);
  await page.waitForFunction(value => Module.textBlocks.some(block => block.text === value), joining ? 'Count: 83' : 'Count: 52');
  await panel.click();
  assert.equal(await page.getByRole('textbox', {name: 'Name', exact: true}).inputValue(), 'café');
  await page.setViewportSize({width: mobile ? 390 : 640, height: 720});
  await page.waitForFunction(() => Module.canvas.height === Math.round(innerHeight * devicePixelRatio));
  console.log(`web ${joining ? "joining" : "font changes"}: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
}

async function checkShapingParagraphs(page, browserType, mobile) {
  const cases = [
    ['שלום ABCDEF אב', true, false], ['WWWWWW אבגד AB', false, false],
    ['שלום\nABCDEF אב', false, false], ['\u202bWWWWWW ABCDEF אב\u202c', true, false],
    ['\u2067WWWWWW ABCDEF אב\u2069', true, false], ['\u202eWWWWWW ABCDEF אב\u202c', true, true],
    ['שלום 12: AB אב!', true, false], ['WWWWWW 12 אב AB!', false, false],
    ['WWWWWW AB\u202aCD\u202cEF אב', false, false],
    ['WWWWWW אב[ אבגדה ] AB', false, false, [['אבגדה ]', 'rtl'], [' AB', 'ltr']]],
    ['שלום עולם ארוך מאוד', true, false, null, true],
    ['אבגדהוזחטיכלמנסע סוף', true, false, null, true]
  ];
  await page.setViewportSize({width: mobile ? 390 : 640, height: 1600});
  await page.waitForFunction(() => Module.canvas.height === Math.round(innerHeight * devicePixelRatio));
  const panel = page.getByRole('button', {name: 'Paragraphs', exact: true});
  await panel.click();
  await page.waitForFunction(source => Module.textBlocks.some(block => block.text === source), cases.at(-1)[0]);
  for (const [index, [source, rtl, override, pieces, ellipsis]] of cases.entries()) {
    const result = await page.evaluate(({source, rtl, override, pieces, ellipsis}) => {
      const block = Module.textBlocks.find(block => block.text === source && !block.lines.some(line => line.action));
      const node = Module.nodes.get(block.id), scale = Module.webFrame.scale;
      const rows = [...new Set(block.lines.map(line => line.y))];
      const row = block.lines.filter(line => line.y === rows[ellipsis ? 0 : 1]);
      const end = Math.max(...row.map(line => line.end));
      const visible = block.text.slice(Math.min(...row.map(line => line.start)), end) + (ellipsis ? "..." : "");
      const x = Math.min(...row.map(line => line.x)), right = Math.max(...row.map(line => line.x + line.width));
      const line = row[0], left = Math.floor(x) - 2, top = Math.floor(line.y - line.font);
      const width = Math.ceil(right) - left + 2, height = Math.ceil(line.font * 1.4);
      const actual = Module.canvas.getContext('2d').getImageData(left, top, width, height).data;
      const background = Module.canvas.getContext('2d').getImageData(Module.canvas.width - 2, 2, 1, 1).data;
      const reference = document.createElement('canvas'); reference.width = width; reference.height = height;
      const ctx = reference.getContext('2d'); ctx.setTransform(scale, 0, 0, scale, 0, 0); ctx.font = line.font / scale + 'px ' + Module.scuzzFontStack;
      ctx.textAlign = 'left'; ctx.direction = rtl ? 'rtl' : 'ltr'; ctx.fillStyle = '#ffffff';
      const mark = override ? '\u202e' : rtl ? '\u200f' : '\u200e';
      if (pieces) {
        let next = x - left;
        for (const [text, direction] of pieces) {
          ctx.direction = direction; ctx.fillText(text, next / scale, (line.y - top) / scale); next += ctx.measureText(text).width * scale;
        }
      } else ctx.fillText(mark + visible + (override ? '\u202c' : ''), (x - left) / scale, (line.y - top) / scale);
      const advance = row.reduce((sum, line) => sum + line.width, 0);
      const referenceAdvance = ctx.measureText(mark + visible + (override ? '\u202c' : '')).width * scale;
      const mask = ctx.getImageData(0, 0, width, height).data;
      const ink = [(line.ink >>> 16) & 255, (line.ink >>> 8) & 255, line.ink & 255];
      let delta = 0;
      for (let p = 0; p < width * height; p++) for (let channel = 0; channel < 3; channel++) {
        const expected = Math.floor((ink[channel] * mask[p * 4 + 3] + background[channel] * (255 - mask[p * 4 + 3])) / 255);
        delta = Math.max(delta, Math.abs(actual[p * 4 + channel] - expected));
      }
      const geometry = node.fragments.map((span, i) => {
        const box = span.getBoundingClientRect(), line = block.lines[i];
        return Math.max(Math.abs(box.left * scale - line.x), Math.abs(box.width * scale - line.width));
      });
      let reverse = true;
      if (override) {
        const index = block.lines.findIndex(line => line.text.includes('ABCDEF'));
        const span = node.fragments[index], range = document.createRange(), start = span.textContent.indexOf('ABCDEF');
        range.setStart(span.firstChild, start + 1); range.collapse(true); const a = range.getBoundingClientRect().x;
        range.setStart(span.firstChild, start + 2); range.collapse(true); reverse = a > range.getBoundingClientRect().x;
      }
      const range = document.createRange();
      range.setStart(node.fragments[0].firstChild, 0);
      range.setEnd(node.fragments.at(-1).firstChild, node.fragments.at(-1).textContent.length);
      getSelection().removeAllRanges(); getSelection().addRange(range);
      let copied;
      const event = new Event('copy', {cancelable: true});
      Object.defineProperty(event, 'clipboardData', {value: {setData(type, value) { if (type === 'text/plain') copied = value; }}});
      document.dispatchEvent(event); getSelection().removeAllRanges();
      let partial;
      if (source.includes('AB\u202aCD\u202cEF')) {
        const joined = node.fragments.map(span => span.textContent).join('');
        const begin = joined.indexOf('ABCDEF');
        const point = offset => {
          for (const span of node.fragments) {
            if (offset <= span.textContent.length) return [span.firstChild, offset];
            offset -= span.textContent.length;
          }
          throw new Error('source offset exceeds the visible paragraph');
        };
        const first = point(begin + 1), last = point(begin + 5);
        const range = document.createRange(); range.setStart(...first); range.setEnd(...last);
        getSelection().addRange(range);
        const event = new Event('copy', {cancelable: true});
        Object.defineProperty(event, 'clipboardData', {value: {setData(type, value) { if (type === 'text/plain') partial = value; }}});
        document.dispatchEvent(event); getSelection().removeAllRanges();
      }
      return {delta, geometry, rows: rows.length, reverse, copied, partial, visible, advance, referenceAdvance, end, dotsLeft: !ellipsis || row.find(line => line.synthetic).x < row.find(line => !line.synthetic).x};
    }, {source, rtl, override, pieces, ellipsis});
    assert(ellipsis ? result.rows === 1 && result.dotsLeft : result.rows > 1, `paragraph ${index}: ${JSON.stringify(result)}`);
    assert(result.delta <= 2, `paragraph ${index}: ${JSON.stringify(result)}`);
    assert(result.geometry.every(delta => delta < 1), `paragraph ${index}: ${JSON.stringify(result)}`);
    assert(result.reverse, `paragraph ${index}: override keeps visual source order`);
    assert.equal(result.copied, ellipsis ? source.slice(0, result.end) : source);
    if (source.includes('AB\u202aCD\u202cEF')) {
      assert.equal(result.partial, 'B\u202aCD\u202cE');
      assert(Math.abs(result.advance - result.referenceAdvance) < .1, JSON.stringify(result));
    }
  }
  const action = page.getByRole('button', {name: 'שלום ABCDEF אב', exact: true});
  if (!mobile) {
    const points = await action.evaluate(node => {
      const first = node.firstChild.getBoundingClientRect(), last = node.lastChild.getBoundingClientRect();
      return {x1: first.x + 2, y1: first.y + first.height / 2, x2: last.right - 2, y2: last.y + last.height / 2};
    });
    await page.mouse.move(points.x1, points.y1); await page.mouse.down();
    await page.mouse.move(points.x2, points.y2, {steps: 8}); await page.mouse.up();
    assert((await page.evaluate(() => getSelection().toString())).length > 0);
    await page.waitForFunction(() => Module.textBlocks.some(block => block.text === 'Count: 12'));
    await page.evaluate(() => getSelection().removeAllRanges());
  }
  await activateInline(page, action, mobile);
  await page.waitForFunction(() => Module.textBlocks.some(block => block.text === 'Count: 29'));
  await panel.click();
  await page.waitForFunction(() => !Module.textBlocks.some(block => block.text === 'WWWWWW אבגד AB'));
  assert.equal(await page.getByRole('textbox', {name: 'Name', exact: true}).inputValue(), 'café');
  await page.setViewportSize({width: mobile ? 390 : 640, height: 720});
  await page.waitForFunction(() => Module.canvas.height === Math.round(innerHeight * devicePixelRatio));
  console.log(`web paragraphs: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
}

async function checkShaping(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    let releaseFonts;
    const fontsReleased = new Promise(resolve => { releaseFonts = resolve; });
    const fontRequests = new Set();
    await context.route(/\/fonts\/[^/]+\.ttf$/, async route => {
      fontRequests.add(new URL(route.request().url()).pathname);
      await fontsReleased;
      await route.continue();
    });
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url, {waitUntil: 'domcontentloaded'});
    await page.waitForFunction(() => Module.ready && Module.textBlocks.some(b => b.text === 'اب AB 12'));
    const initialField = page.getByRole('textbox', {name: 'Name', exact: true});
    await initialField.focus();
    await initialField.evaluate(node => { node.setSelectionRange(1, 1); node.dispatchEvent(new Event('select')); });
    const identity = await initialField.getAttribute('id');
    const paintsBeforeFonts = await page.evaluate(() => Module.ccall('sz_web_paints', 'number', [], []));
    releaseFonts();
    await page.evaluate(async () => {
      await Promise.all([document.fonts.load('24px "Scuzz devanagari"', 'नमस्ते'),
        document.fonts.load('24px "Scuzz arabic"', 'مر'), document.fonts.load('24px "Scuzz thai"', 'สวัสดี'), document.fonts.load('24px "Scuzz sc"', '中文')]);
      await document.fonts.ready;
    });
    await page.waitForFunction(paints => [...document.fonts].filter(face => face.family.includes('Scuzz ')).every(face => face.status === 'loaded') && Module.ccall('sz_web_paints', 'number', [], []) > paints, paintsBeforeFonts);
    assert.equal(fontRequests.size, 4, 'load the four pinned fonts');
    assert.equal(await initialField.getAttribute('id'), identity, 'font loading keeps field identity');
    assert(await initialField.evaluate(node => node === document.activeElement && node.selectionStart === 1 && node.selectionEnd === 1), 'font loading keeps focus and selection');
    assert.equal(await initialField.inputValue(), 'אב', 'font loading keeps source state');
    const expectText = async text => page.waitForFunction(text => Module.textBlocks.some(b => b.text === text), text);
    const comparePixels = async (source, pieces) => {
      const result = await page.evaluate(({source, pieces}) => {
        const block = Module.textBlocks.find(b => b.text === source);
        const parts = block.lines.filter(line => line.y === block.lines[0].y);
        const startX = Math.min(...parts.map(line => line.x));
        const line = {...parts[0], x: startX, width: Math.max(...parts.map(line => line.x + line.width)) - startX};
        const left = Math.floor(line.x), top = Math.floor(line.y - line.font);
        const width = Math.ceil(line.width + 2), height = Math.ceil(line.font * 1.4);
        const actual = Module.canvas.getContext('2d').getImageData(left, top, width, height).data;
        const background = Module.canvas.getContext('2d').getImageData(Module.canvas.width - 2, 2, 1, 1).data;
        const reference = document.createElement('canvas'); reference.width = width; reference.height = height;
        const ctx = reference.getContext('2d', {willReadFrequently: true});
        const scale = Module.webFrame.scale; ctx.setTransform(scale, 0, 0, scale, 0, 0);
        ctx.fillStyle = '#ffffff'; ctx.font = line.font / scale + 'px ' + Module.scuzzFontStack; ctx.textAlign = 'left';
        let x = line.x - left;
        for (const [text, direction] of pieces) {
          ctx.direction = direction; ctx.fillText(text, x / scale, (line.y - top) / scale); x += ctx.measureText(text).width * scale;
        }
        const expected = ctx.getImageData(0, 0, width, height).data;
        const ink = [(line.ink >>> 16) & 255, (line.ink >>> 8) & 255, line.ink & 255];
        let delta = 0, worst;
        for (let i = 0; i < actual.length; i++) {
          const channel = i % 4, alpha = expected[i - channel + 3];
          const value = channel === 3 ? 255 : Math.floor((ink[channel] * alpha + background[channel] * (255 - alpha)) / 255);
          const difference = Math.abs(actual[i] - value);
          if (difference > delta) { delta = difference; worst = {x: Math.floor(i / 4) % width, y: Math.floor(i / 4 / width), actual: actual[i], value, alpha}; }
        }
        return {delta, worst, direction: Module.scuzzDirections?.get(source), width: line.width, reference: x - (line.x - left)};
      }, {source, pieces});
      assert(Math.abs(result.width - result.reference) < 1, JSON.stringify(result));
      assert(result.delta <= 2, `${browserType.name()} ${source}: ${JSON.stringify(result)}`);
    };
    await comparePixels('AV fi é', [['AV fi é', 'ltr']]);
    await comparePixels('اب AB 12', [['AB 12', 'ltr'], [' ', 'ltr'], ['اب', 'rtl']]);
    await comparePixels('ab אב 12', [['ab ', 'ltr'], ['12', 'ltr'], [' ', 'ltr'], ['אב', 'rtl']]);
    await comparePixels('क्षि ปู่ 中文', [['क्षि ปู่ 中文', 'ltr']]);
    await comparePixels('नमस्ते สวัสดี 中文漢字', [['नमस्ते สวัสดี 中文漢字', 'ltr']]);
    for (const source of ['السلام عليكم', 'تفاصيل']) {
      const result = await page.evaluate(source => {
        const block = Module.textBlocks.find(block => block.text === source);
        const node = Module.nodes.get(block.id), lines = block.lines;
        const scale = Module.webFrame.scale;
        const boxes = node.fragments.map(span => span.getBoundingClientRect());
        const geometry = boxes.map((box, i) => Math.max(Math.abs(box.left * scale - lines[i].x), Math.abs(box.width * scale - lines[i].width)));
        const x = Math.min(...lines.map(line => line.x));
        const end = Math.max(...lines.map(line => line.x + line.width));
        const left = Math.max(0, Math.floor(x) - 2), top = Math.max(0, Math.floor(lines[0].y - lines[0].font * 1.5));
        const width = Math.min(Module.canvas.width - left, Math.ceil(end) - left + 2);
        const height = Math.ceil(lines[0].font * 2);
        const actual = Module.canvas.getContext('2d').getImageData(left, top, width, height).data;
        const background = Module.canvas.getContext('2d').getImageData(Module.canvas.width - 2, 2, 1, 1).data;
        const reference = document.createElement('canvas'); reference.width = width; reference.height = height;
        const ctx = reference.getContext('2d'); ctx.setTransform(scale, 0, 0, scale, 0, 0); ctx.font = lines[0].font / scale + 'px ' + Module.scuzzFontStack;
        ctx.textAlign = 'left'; ctx.direction = 'rtl'; ctx.fillStyle = '#ffffff';
        ctx.fillText(source, (x - left) / scale, (lines[0].y - top) / scale);
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        if (lines[0].style & 8) {
          const underline = lines[0].y + lines[0].font * .1;
          ctx.fillRect(Math.floor(x) - left, Math.floor(underline) - top, Math.floor(end) - Math.floor(x),
            Math.floor(underline + Math.max(1, lines[0].font / 16)) - Math.floor(underline));
        }
        const mask = ctx.getImageData(0, 0, width, height).data;
        let delta = 0;
        for (let p = 0; p < width * height; p++) {
          const at = left + p % width;
          // Use device-pixel paint bounds. Source boxes can have fractional widths.
          const line = lines.find(line => at >= Math.round(line.x) && at < Math.round(line.x + line.width)) || (at < x ? lines.at(-1) : lines[0]);
          const ink = [(line.ink >>> 16) & 255, (line.ink >>> 8) & 255, line.ink & 255];
          for (let channel = 0; channel < 3; channel++) {
            const expected = Math.floor((ink[channel] * mask[p * 4 + 3] + background[channel] * (255 - mask[p * 4 + 3])) / 255);
            delta = Math.max(delta, Math.abs(expected - actual[p * 4 + channel]));
          }
        }
        const range = document.createRange();
        range.setStart(node.fragments[0].firstChild, 0);
        range.setEnd(node.fragments.at(-1).firstChild, node.fragments.at(-1).textContent.length);
        getSelection().removeAllRanges(); getSelection().addRange(range);
        let copied;
        const event = new Event('copy', {cancelable: true});
        Object.defineProperty(event, 'clipboardData', {value: {setData(type, value) { if (type === 'text/plain') copied = value; }}});
        document.dispatchEvent(event); getSelection().removeAllRanges();
        return {delta, geometry, copied, pieces: lines.map(line => line.text), inks: lines.map(line => line.ink), width: end - x, reference: ctx.measureText(source).width * scale};
      }, source);
      assert(result.geometry.every(delta => delta < 1), JSON.stringify(result));
      assert(Math.abs(result.width - result.reference) < 1, JSON.stringify(result));
      assert(result.delta <= 2, JSON.stringify(result));
      assert.equal(result.copied, source);
      assert(result.inks.some(ink => (ink >>> 0) === 0xff165ca0), JSON.stringify(result));
      assert(result.pieces.length > 1, JSON.stringify(result));
    }
    const fontWidths = await page.evaluate(() => {
      const ctx = document.createElement('canvas').getContext('2d');
      return [['Scuzz devanagari', 'क्षि'], ['Scuzz thai', 'ปู่'], ['Scuzz sc', '中文漢字']].map(([font, source]) => {
        ctx.font = '1000px "' + font + '"'; return ctx.measureText(source).width;
      });
    });
    for (const [index, width] of [976, 605, 4000].entries())
      assert(Math.abs(fontWidths[index] - width) < .1, 'pinned font advances at weight 400: ' + JSON.stringify(fontWidths));
    const clusters = await page.evaluate(() => Module.textBlocks.filter(b => b.text === 'é'.repeat(10)).map(b => b.lines));
    assert.equal(clusters.length, 2);
    for (const lines of clusters) for (const line of lines) {
      assert(!line.text.startsWith('́'), 'a visible line starts with its base');
      assert(line.synthetic || line.text.endsWith('́'), 'a visible line keeps its accent');
    }
    assert(clusters.some(lines => lines.length > 1), 'narrow text wraps');
    assert(clusters.some(lines => lines.some(line => line.synthetic)), 'ellipsis is synthetic');
    for (const source of ['AV fi é', 'اب AB 12', 'ab אב 12', 'नमस्ते สวัสดี 中文漢字', 'क्षि ปู่ 中文']) {
      const copied = await page.evaluate(source => {
        const item = Module.textBlocks.find(b => b.text === source);
        const node = Module.nodes.get(item.id), first = node.fragments[0], last = node.fragments.at(-1);
        const range = document.createRange(); range.setStart(first.firstChild, 0); range.setEnd(last.firstChild, last.textContent.length);
        const selection = getSelection(); selection.removeAllRanges(); selection.addRange(range);
        let copied = '';
        const event = new Event('copy', {bubbles: true, cancelable: true});
        Object.defineProperty(event, 'clipboardData', {value: {setData(type, value) { if (type === 'text/plain') copied = value; }}});
        document.dispatchEvent(event); selection.removeAllRanges(); return copied;
      }, source);
      assert.equal(copied, source, 'copy preserves logical source bytes');
    }
    const field = page.getByRole('textbox', {name: 'Name', exact: true});
    assert.equal(await field.inputValue(), 'אב');
    assert.equal(await field.getAttribute('dir'), 'auto');
    assert.equal(await field.evaluate(node => getComputedStyle(node).textAlign), 'left');
    await field.focus();
    await field.evaluate(node => { node.setSelectionRange(0, 0); node.dispatchEvent(new Event('select')); });
    await page.keyboard.press('ArrowLeft');
    assert.equal(await field.evaluate(node => node.selectionStart), 1);
    await page.keyboard.insertText('X');
    await expectText('Value: אXב');
    const action = page.getByRole('button', {name: 'تفاصيل', exact: true});
    if (mobile) await action.tap(); else await action.click();
    await expectText('Count: 11');
    await page.getByRole('button', {name: 'Set Arabic', exact: true}).click();
    await expectText('Value: لا');
    assert.equal(await field.inputValue(), 'لا');
    await page.getByRole('button', {name: 'Set Latin', exact: true}).click();
    await expectText('Value: AV fi é');
    assert.equal(await field.inputValue(), 'AV fi é');
    await field.fill('café'); await expectText('Value: café');
    await page.getByRole('button', {name: 'Add', exact: true}).click(); await expectText('Count: 12');
    const scroll = await page.evaluate(() => {
      const line = Module.textBlocks.find(b => b.text === 'M'.repeat(400)).lines[0];
      return {end: line.end, displayed: line.text.length, clip: line.clip, scale: Module.webFrame.scale};
    });
    assert.equal(scroll.end, 400, 'long lines retain the last source character');
    assert.equal(scroll.displayed, 400, 'paint keeps the complete source line');
    assert(scroll.clip);
    if (mobile) {
      await page.evaluate(({clip, scale}) => {
        const target = Module.canvas, y = (clip[1] + clip[3] / 2) / scale;
        const send = (type, x) => {
          const touch = {identifier: 0, target, clientX: x, clientY: y};
          const touches = type === 'touchend' ? [] : [touch];
          const event = new Event(type, {bubbles: true, cancelable: true});
          Object.defineProperties(event, {touches: {value: touches}, targetTouches: {value: touches}, changedTouches: {value: [touch]}});
          target.dispatchEvent(event);
        };
        send('touchstart', (clip[0] + clip[2] - 12) / scale);
        send('touchmove', (clip[0] + 12) / scale);
        send('touchend', (clip[0] + 12) / scale);
      }, scroll);
    } else {
      await page.mouse.move((scroll.clip[0] + scroll.clip[2] / 2) / scroll.scale, (scroll.clip[1] + scroll.clip[3] / 2) / scroll.scale);
      await page.mouse.wheel(10000, 0);
    }
    await page.waitForFunction(() => Module.textBlocks.find(b => b.text === 'M'.repeat(400)).lines[0].x < 0);
    await page.setViewportSize({width: 220, height: 720});
    await page.waitForFunction(() => Module.canvas.width === Math.round(innerWidth * devicePixelRatio));
    assert.equal(await field.inputValue(), 'café');
    await page.setViewportSize({width: mobile ? 390 : 640, height: 720});
    await page.waitForFunction(() => Module.canvas.width === Math.round(innerWidth * devicePixelRatio));
    await expectText('Count: 12');
    await checkShapingParagraphs(page, browserType, mobile);
    await checkShapingFontChanges(page, browserType, mobile);
    await checkShapingFontChanges(page, browserType, mobile, true);
    assert.deepEqual(errors, []);
    await context.close();
    console.log(`web shaping: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkTooltips(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 480},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame.items.some(item => item.tooltip));
    const button = label => page.getByRole('button', {name: label, exact: true});
    const save = button('Save'), next = button('Next'), nested = button('Nested');
    const name = page.getByRole('textbox', {name: 'Name', exact: true});
    const focusIs = async node => assert(await node.evaluate(node => document.activeElement === node));
    const visible = async (label, open) => page.waitForFunction(({label, open}) =>
      Module.webFrame.items.some(item => item.tooltip && item.label === label && item.hidden === !open), {label, open});
    const description = async node => {
      const id = await node.getAttribute('aria-describedby');
      assert(id, 'control has a description');
      const tooltip = page.locator('#' + id);
      assert.equal(await tooltip.getAttribute('role'), 'tooltip');
      assert.equal(await tooltip.getAttribute('tabindex'), null);
      return tooltip.textContent();
    };
    assert.equal(await description(save), 'Save the current document');
    assert.equal(await description(name), 'Enter your full name');
    assert.equal(await description(nested), 'Inner help');
    assert.equal(await button('Empty help').getAttribute('aria-describedby'), null);
    await visible('Save the current document', false);
    await page.locator('#canvas').focus();
    await page.keyboard.press('Tab');
    await focusIs(save);
    await visible('Save the current document', true);
    await page.keyboard.press('Escape');
    await focusIs(save);
    await visible('Save the current document', false);
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => Module.textBlocks.some(b => b.text === 'Saved: 1'));
    await visible('Save the current document', false);
    await page.keyboard.press('Tab'); await focusIs(next);
    await page.keyboard.press('Tab'); await focusIs(name);
    await visible('Enter your full name', true);
    await name.fill('kept');
    await page.keyboard.press('Escape');
    await visible('Enter your full name', false);
    await page.keyboard.press('Tab');
    await focusIs(button('Open details'));
    await page.keyboard.press('Enter');
    await visible('Close the details dialog', true);
    await focusIs(button('Close'));
    assert.equal(await save.count(), 0, 'background controls leave the accessibility tree');
    await page.keyboard.press('Escape');
    await visible('Close the details dialog', false);
    assert.equal(await page.getByRole('dialog', {name: 'Details'}).count(), 1);
    await focusIs(button('Close'));
    await page.keyboard.press('Escape');
    await page.waitForFunction(() => !Module.webFrame.items.some(item => item.group && item.role === 43));
    await focusIs(button('Open details'));
    await page.keyboard.press('Tab'); await focusIs(nested);
    await visible('Inner help', true); await visible('Outer help', false);
    await page.keyboard.press('Tab');
    await page.keyboard.press('Tab'); await focusIs(button('Long help'));
    const longText = await description(button('Long help'));
    await visible(longText, true);
    const bounds = await page.getByRole('tooltip', {name: longText, exact: true}).boundingBox();
    assert(bounds && bounds.x >= 0 && bounds.y >= 0);
    assert(bounds.x + bounds.width <= (mobile ? 390 : 640) + 1 && bounds.y + bounds.height <= 481);
    assert(bounds.height > 40, 'long help wraps');
    await page.setViewportSize({width: 220, height: 480});
    await page.waitForFunction(() => Module.canvas.width === Math.round(innerWidth * devicePixelRatio));
    await visible(longText, true);
    await focusIs(button('Long help'));
    const narrow = await page.getByRole('tooltip', {name: longText, exact: true}).boundingBox();
    assert(narrow && narrow.x + narrow.width <= 221);
    await page.setViewportSize({width: mobile ? 390 : 640, height: 480});
    await page.waitForFunction(() => Module.canvas.width === Math.round(innerWidth * devicePixelRatio));
    await page.keyboard.press('Tab'); await focusIs(button('Empty help'));
    await visible(longText, false);
    assert.equal(await page.getByRole('tooltip').count(), 0);
    assert.equal(await name.inputValue(), 'kept');
    await page.getByRole('checkbox', {name: 'Show help', exact: true}).click();
    await page.waitForFunction(() => !Module.webFrame.items.some(item => item.label === 'Long help'));
    assert.equal(await button('Long help').count(), 0);
    assert.equal(await page.locator('[role=tooltip]').filter({hasText: longText}).count(), 0);
    await page.getByRole('checkbox', {name: 'Show help', exact: true}).click();
    await page.waitForFunction(() => Module.webFrame.items.some(item => item.label === 'Long help'));
    await page.locator('#canvas').focus();
    if (!mobile) {
      const trigger = await save.boundingBox();
      await page.mouse.move(trigger.x + trigger.width / 2, trigger.y + trigger.height / 2);
      await visible('Save the current document', true);
      const popup = page.getByRole('tooltip', {name: 'Save the current document', exact: true});
      const rect = await popup.boundingBox();
      await page.mouse.move(trigger.x + trigger.width / 2, rect.y + 2, {steps: 8});
      await page.mouse.move(rect.x + rect.width / 2, rect.y + rect.height / 2, {steps: 8});
      await visible('Save the current document', true);
      await popup.click();
      await page.waitForFunction(() => Module.textBlocks.some(b => b.text === 'Saved: 1'));
      await page.mouse.move(trigger.x + trigger.width / 2, rect.y + 2);
      await page.mouse.move(trigger.x + trigger.width / 2, trigger.y + trigger.height / 2);
      await page.keyboard.press('Escape');
      await visible('Save the current document', false);
      await page.mouse.move(trigger.x + 2, trigger.y + 2);
      await visible('Save the current document', false);
      await page.mouse.move(600, 450);
      await page.mouse.move(trigger.x + 2, trigger.y + 2);
      await visible('Save the current document', true);
      await page.mouse.move(600, 450);
      await visible('Save the current document', false);
    }
    await save.focus(); await visible('Save the current document', true);
    await page.evaluate(() => {
      const outside = document.createElement('button'); outside.id = 'outside'; outside.textContent = 'Outside';
      document.body.append(outside); outside.focus();
    });
    await visible('Save the current document', false);
    assert.deepEqual(errors, []);
    await context.close();
    console.log(`web tooltips: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkInline(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame.items.some(item => item.inline));
    const add = page.getByRole('button', {name: 'add one', exact: true});
    const reset = page.getByRole('button', {name: 'reset', exact: true});
    const details = page.getByRole('button', {name: 'open the details dialog across wrapped lines', exact: true});
    const minimum = page.getByRole('button', {name: 'Minimum', exact: true});
    const maximum = page.getByRole('button', {name: 'Maximum', exact: true});
    const partial = page.getByRole('button', {name: 'abcdefghijklmno', exact: true});
    const name = page.getByRole('textbox', {name: 'Name', exact: true});
    const expectText = async text => page.waitForFunction(text => Module.textBlocks.some(block => block.text === text), text);
    const focus = async node => assert.equal(await node.evaluate(node => document.activeElement === node), true);
    const activate = node => activateInline(page, node, mobile);
    assert.equal(await add.count(), 2);
    assert.equal(await page.getByRole('button', {name: 'Hidden action', exact: true}).count(), 0);
    assert.equal(await page.getByRole('button').count(), 9);
    const metadata = await details.evaluate(node => ({boxes: node.item.boxes.length, parent: node.parentElement.id,
      order: [...node.parentElement.querySelectorAll('[role=button]')].map(button => button.getAttribute('aria-label'))}));
    assert.ok(metadata.boxes > 1);
    assert.deepEqual(metadata.order, ['add one', 'reset', 'open the details dialog across wrapped lines', 'add one']);
    await page.evaluate(() => Module.canvas.focus());
    await page.keyboard.press('Tab'); await focus(add.nth(0));
    const prefixPoint = await page.evaluate(() => {
      const block = Module.textBlocks.find(block => block.text.startsWith('You can '));
      const span = Module.nodes.get(block.id).fragments[0];
      const frame = span.getBoundingClientRect();
      return {x: frame.x + frame.width / 2, y: frame.y + frame.height / 2};
    });
    if (mobile) await page.touchscreen.tap(prefixPoint.x, prefixPoint.y); else await page.mouse.click(prefixPoint.x, prefixPoint.y);
    await expectText('Last action: 0');
    if (!mobile) {
      const edge = await add.nth(0).evaluate(node => {
        const [x, y, width, height] = node.item.boxes[0], scale = Module.webFrame.scale;
        return {x: (x + width / 2) / scale, y: (y + height) / scale};
      });
      await page.mouse.move(edge.x, edge.y - 1); await page.mouse.down();
      await page.mouse.move(edge.x, edge.y + 2); await page.mouse.up();
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      await expectText('Last action: 0');
      await page.evaluate(() => getSelection().removeAllRanges());
    }
    if (!mobile) {
      const endpoints = await details.evaluate(node => {
        const first = node.firstChild.getBoundingClientRect();
        const last = node.lastChild.getBoundingClientRect();
        return {x1: first.x + 2, y1: first.y + first.height / 2,
          x2: last.x + last.width - 2, y2: last.y + last.height / 2};
      });
      await page.mouse.move(endpoints.x1, endpoints.y1); await page.mouse.down();
      await page.mouse.move(endpoints.x2, endpoints.y2, {steps: 6}); await page.mouse.up();
      assert.ok((await page.evaluate(() => getSelection().toString())).length > 0);
      await expectText('Last action: 0');
      await page.evaluate(() => getSelection().removeAllRanges());
    }
    const copied = await page.evaluate(() => {
      const block = Module.textBlocks.find(block => block.text.startsWith('You can '));
      const spans = Module.nodes.get(block.id).fragments;
      const range = document.createRange();
      range.setStart(spans[0].firstChild, 0);
      range.setEnd(spans.at(-1).firstChild, spans.at(-1).textContent.length);
      getSelection().removeAllRanges(); getSelection().addRange(range);
      let copy = '';
      const event = new Event('copy', {cancelable: true});
      Object.defineProperty(event, 'clipboardData', {value: {setData(type, text) { if (type === 'text/plain') copy = text; }}});
      document.dispatchEvent(event);
      const result = {source: block.text, copy};
      getSelection().removeAllRanges();
      return result;
    });
    assert.equal(copied.copy, copied.source);
    await add.nth(0).focus(); await focus(add.nth(0));
    await page.keyboard.press('Enter'); await expectText('Count: 1'); await expectText('Last action: 1');
    await page.waitForFunction(() => document.title === 'Action 1');
    await page.keyboard.press('Tab'); await focus(reset);
    await page.keyboard.press('Space'); await expectText('Count: 0');
    await page.keyboard.press('Tab'); await focus(details);
    await page.keyboard.press('Enter');
    const dialog = page.getByRole('dialog', {name: 'Inline details', exact: true});
    await dialog.waitFor();
    await page.keyboard.press('Enter');
    await details.waitFor(); await focus(details);
    await page.keyboard.press('Tab'); await focus(add.nth(1));
    await page.keyboard.press('Space'); await expectText('Count: 1');
    await page.keyboard.press('Tab'); await focus(minimum);
    await page.keyboard.press('Enter'); await expectText('Last action: -9223372036854775808');
    await page.keyboard.press('Tab'); await focus(maximum);
    await page.keyboard.press('Enter'); await expectText('Last action: 9223372036854775807');
    await page.keyboard.press('Tab'); await focus(partial);
    await page.keyboard.press('Enter'); await expectText('Last action: 5');
    await page.keyboard.press('Tab'); await focus(page.getByRole('button', {name: 'Named handler', exact: true}));
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => document.title === 'Named action');
    await page.keyboard.press('Tab'); await focus(page.getByRole('button', {name: 'Value handler', exact: true}));
    await page.keyboard.press('Enter'); await expectText('Last action: 6');
    await page.keyboard.press('Tab'); await focus(name);
    await name.fill('retained');
    const dots = await partial.evaluate(node => {
      const span = node.parentElement.fragments.find(span => span.synthetic);
      const frame = span.getBoundingClientRect();
      return {x: frame.x + frame.width / 2, y: frame.y + frame.height / 2};
    });
    if (mobile) await page.touchscreen.tap(dots.x, dots.y); else await page.mouse.click(dots.x, dots.y);
    await expectText('Last action: 6');
    await activate(add.nth(1)); await expectText('Count: 2'); await expectText('Last action: 1');
    await activate(reset); await expectText('Count: 0');
    await activate(details); await dialog.waitFor();
    await page.getByRole('button', {name: 'Close', exact: true}).click();
    await details.waitFor(); await focus(details);
    assert.equal(await name.inputValue(), 'retained');
    await page.setViewportSize({width: 220, height: 720});
    await page.waitForFunction(() => Math.round(Module.canvas.width / Module.webFrame.scale) === 220);
    await focus(details);
    await page.setViewportSize({width: 640, height: 720});
    await page.waitForFunction(() => Math.round(Module.canvas.width / Module.webFrame.scale) === 640);
    await focus(details);
    assert.equal(await name.inputValue(), 'retained');
    assert.deepEqual(errors, []);
    console.log(`web inline: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkDisclosure(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.nodes.size > 0);
    const header = page.getByRole('button', {name: 'Details', exact: true});
    const nested = page.getByRole('button', {name: 'More details', exact: true});
    const conditional = page.getByRole('button', {name: 'Conditional', exact: true});
    const name = page.getByRole('textbox', {name: 'Name', exact: true});
    const note = page.getByRole('textbox', {name: 'Note', exact: true});
    const state = async (button, expanded) => page.waitForFunction(({node, expanded}) =>
      node.getAttribute('aria-expanded') === String(expanded), {node: await button.elementHandle(), expanded});
    const focused = async node => assert.equal(await node.evaluate(node => document.activeElement === node), true);
    const activate = async node => {
      if (mobile) { const frame = await node.boundingBox(); await page.touchscreen.tap(frame.x + frame.width / 2, frame.y + frame.height / 2); }
      else await node.click();
    };
    await state(header, false);
    const controlled = await header.getAttribute('aria-controls');
    const region = page.locator('#' + controlled);
    assert.equal(await region.getAttribute('role'), 'region');
    assert.equal(await region.getAttribute('aria-labelledby'), await header.getAttribute('id'));
    assert.equal(await region.getAttribute('hidden'), '');
    assert.equal(await name.count(), 0);
    assert.equal(await page.getByText('Conditional body', {exact: true}).count(), 0);
    await page.evaluate(() => Module.canvas.focus());
    await page.keyboard.press('Tab'); await focused(header);
    await page.keyboard.press('Enter'); await state(header, true);
    const headFrame = await header.boundingBox();
    const bodyFrame = await region.boundingBox();
    assert.ok(headFrame.height < bodyFrame.height && headFrame.y + headFrame.height <= bodyFrame.y + .1);
    const passive = page.getByText('Select this body text.', {exact: true});
    await activate(passive); await state(header, true);
    if (!mobile) {
      const frame = await passive.boundingBox();
      await page.mouse.move(frame.x + 1, frame.y + frame.height / 2);
      await page.mouse.down();
      await page.mouse.move(frame.x + frame.width - 1, frame.y + frame.height / 2);
      await page.mouse.up();
      assert.ok((await page.evaluate(() => getSelection().toString())).includes('body text'));
      await state(header, true);
    }
    await header.focus();
    await page.keyboard.press('Tab'); await focused(name);
    await name.fill('retained');
    await page.keyboard.press('Tab');
    await focused(page.getByRole('button', {name: 'Add', exact: true}));
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => Module.textBlocks.some(block => block.text === 'Count: 1'));
    await page.keyboard.press('Tab'); await focused(nested);
    await page.keyboard.press('Space'); await state(nested, true);
    await page.keyboard.press('Tab'); await focused(note);
    await note.fill('nested text');
    await page.keyboard.press('Tab');
    await focused(page.getByRole('button', {name: 'Close details', exact: true}));
    await page.keyboard.press('Enter'); await state(header, false); await focused(header);
    assert.equal(await name.count(), 0);
    assert.equal(await note.count(), 0);
    await page.keyboard.press('Space'); await state(header, true); await state(nested, true);
    assert.equal(await name.inputValue(), 'retained');
    assert.equal(await note.inputValue(), 'nested text');
    await activate(page.getByRole('checkbox', {name: 'Show conditional body', exact: true}));
    await page.waitForFunction(() => Module.textBlocks.some(block => block.text === 'Conditional body'));
    await conditional.focus(); await page.keyboard.press('Space'); await state(conditional, false);
    assert.equal(await page.getByText('Conditional body', {exact: true}).count(), 0);
    await page.keyboard.press('Enter'); await state(conditional, true);
    await page.waitForFunction(() => Module.textBlocks.some(block => block.text === 'Conditional body'));
    const hide = page.getByRole('button', {name: 'Hide details', exact: true});
    await activate(hide); await state(header, false); await focused(hide);
    const open = page.getByRole('button', {name: 'Open details', exact: true});
    await activate(open); await state(header, true); await focused(open);
    assert.equal(await name.inputValue(), 'retained');
    assert.equal(await note.inputValue(), 'nested text');
    assert.deepEqual(errors, []);
    console.log(`web disclosure: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkChoices(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 600},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.nodes.size > 0);
    const group = label => page.getByRole('radiogroup', {name: label, exact: true});
    const layout = group('Layout').getByRole('radio');
    const density = group('Density').getByRole('radio');
    const repeated = group('Repeated labels').getByRole('radio');
    const only = group('One choice').getByRole('radio');
    const name = page.getByRole('textbox', {name: 'Name', exact: true});
    const selected = async (radios, index, focused = true) => {
      await page.waitForFunction(({nodes, index, focused}) => nodes.every((node, i) =>
        node.getAttribute('aria-checked') === String(i === index) && node.tabIndex === (i === index ? 0 : -1)) &&
        (!focused || document.activeElement === nodes[index]), {nodes: await radios.elementHandles(), index, focused});
    };
    assert.equal(await layout.count(), 3);
    assert.equal(await density.count(), 4);
    assert.equal(await repeated.count(), 3);
    assert.equal(await only.count(), 1);
    assert.equal(await group('No choices').getByRole('radio').count(), 0);
    await selected(layout, 1, false);
    await selected(density, 0, false);
    await selected(repeated, 2, false);
    assert.equal(await only.getAttribute('aria-checked'), 'false');
    assert.equal(await only.getAttribute('tabindex'), '0');
    const frames = await layout.evaluateAll(nodes => nodes.map(node => node.getBoundingClientRect().toJSON()));
    assert.ok(frames.every(frame => Math.abs(frame.width - frames[0].width) < .1));
    assert.ok(Math.abs(frames[0].right - frames[1].left) < .1 && Math.abs(frames[1].right - frames[2].left) < .1);
    await page.evaluate(() => Module.canvas.focus());
    await page.keyboard.press('Tab'); await selected(layout, 1);
    for (const [key, index] of [['ArrowRight', 2], ['ArrowDown', 0], ['ArrowLeft', 2], ['Home', 0], ['End', 2]]) {
      await page.keyboard.press(key); await selected(layout, index);
    }
    await page.keyboard.press('q');
    await page.keyboard.press('Control+ArrowLeft'); await selected(layout, 2);
    assert.equal(await name.inputValue(), '');
    await page.keyboard.press('Tab'); await selected(density, 0);
    for (const [key, index] of [['End', 3], ['ArrowLeft', 2], ['ArrowDown', 3], ['ArrowRight', 0]]) {
      await page.keyboard.press(key); await selected(density, index);
    }
    await page.keyboard.press('Tab'); await selected(repeated, 2);
    for (const [key, index] of [['ArrowRight', 0], ['ArrowLeft', 2], ['Home', 0], ['End', 2]]) {
      await page.keyboard.press(key); await selected(repeated, index);
    }
    await page.keyboard.press('Tab');
    assert.equal(await only.evaluate(node => document.activeElement === node), true);
    await page.keyboard.press('Space'); await selected(only, 0);
    await page.keyboard.press('Tab');
    assert.equal(await name.evaluate(node => document.activeElement === node), true);
    await name.fill('retained');
    await page.keyboard.press('Shift+Tab'); await selected(only, 0);
    await page.keyboard.press('Shift+Tab'); await selected(repeated, 2);
    const activate = async node => {
      if (mobile) { const frame = await node.boundingBox(); await page.touchscreen.tap(frame.x + frame.width / 2, frame.y + frame.height / 2); }
      else await node.click();
    };
    await activate(layout.nth(0)); await selected(layout, 0);
    await activate(layout.nth(2)); await selected(layout, 2);
    await activate(density.nth(3)); await selected(density, 3);
    await activate(repeated.nth(1)); await selected(repeated, 1);
    assert.equal(await name.inputValue(), 'retained');
    await page.getByRole('button', {name: 'Reset', exact: true}).click();
    await selected(layout, 1, false);
    await selected(density, 0, false);
    await selected(repeated, 2, false);
    await page.waitForFunction(() => Module.textBlocks.some(block => block.text === 'Layout: 1') &&
      Module.textBlocks.some(block => block.text === 'Density: 0') && Module.textBlocks.some(block => block.text === 'Repeated: 2'));
    assert.deepEqual(errors, []);
    console.log(`web choices: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkSplit(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 640, height: 480},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame.items.some(item => item.role === 42));
    const separator = page.getByRole('separator', {name: 'Tools', exact: true});
    const primary = page.getByRole('textbox', {name: 'Primary note', exact: true});
    const secondary = page.getByRole('textbox', {name: 'Secondary note', exact: true});
    const value = async n => page.waitForFunction(n => {
      const node = document.querySelector('[role=separator]');
      return node?.getAttribute('aria-valuenow') === String(n) &&
        Module.textBlocks.some(block => block.text === `Primary size: ${n}%`);
    }, n);
    assert.equal(await separator.getAttribute('aria-orientation'), 'vertical');
    assert.equal(await separator.getAttribute('aria-valuemin'), '0');
    assert.equal(await separator.getAttribute('aria-valuemax'), '100');
    const controlled = await separator.getAttribute('aria-controls');
    assert.equal(await page.locator('#' + controlled).getAttribute('role'), 'region');
    assert.equal(await page.locator('#' + controlled).getAttribute('aria-label'), 'Tools');
    await primary.fill('retained');
    await page.keyboard.press('Tab');
    assert.equal(await page.evaluate(() => document.activeElement.getAttribute('aria-label')), 'Add');
    await page.keyboard.press('Tab');
    assert.equal(await page.evaluate(() => document.activeElement.getAttribute('role')), 'separator');
    await page.keyboard.press('ArrowRight'); await value(41);
    await page.keyboard.press('ArrowLeft'); await value(40);
    await page.keyboard.press('Enter'); await value(0);
    assert.equal(await primary.count(), 0);
    assert.equal(await page.getByRole('button', {name: 'Add', exact: true}).count(), 0);
    assert.equal(await page.locator('#' + controlled).getAttribute('hidden'), '');
    await page.keyboard.press('Tab');
    assert.equal(await page.evaluate(() => document.activeElement.getAttribute('aria-label')), 'Secondary note');
    await secondary.fill('preview');
    await page.keyboard.press('Shift+Tab');
    await page.keyboard.press('Enter'); await value(40);
    assert.equal(await primary.inputValue(), 'retained');
    await page.keyboard.press('Home'); await value(0);
    await page.keyboard.press('Enter'); await value(40);
    await page.keyboard.press('End'); await value(100);
    assert.equal(await secondary.count(), 0);
    await page.keyboard.press('ArrowRight'); await value(100);
    await page.keyboard.press('ArrowLeft'); await value(99);
    assert.equal(await secondary.inputValue(), 'preview');
    await page.keyboard.press('q');
    await page.keyboard.press('Control+ArrowLeft'); await value(99);
    assert.equal(await primary.inputValue(), 'retained');
    await page.getByRole('button', {name: 'Reset panes', exact: true}).click();
    await value(40);
    const track = await page.evaluate(() => {
      const item = Module.webFrame.items.find(item => item.role === 42);
      const pane = Module.webFrame.items.find(candidate => candidate.id === item.related);
      const scale = Module.webFrame.scale;
      const width = (item.x - pane.x) * 100 / Number(item.range.value);
      return {x: (item.x + item.w / 2) / scale, y: (item.y + item.h / 2) / scale,
        target: (pane.x + item.w / 2 + width * .7) / scale};
    });
    if (mobile) {
      await page.touchscreen.tap(track.x, track.y); await value(40);
    } else {
      await page.mouse.move(track.x, track.y); await page.mouse.down(); await value(40);
      await page.mouse.move(track.target, track.y);
      await value(70);
      await page.mouse.move(630, track.y); await value(100);
      await page.mouse.move(1, track.y); await value(0);
      await page.mouse.up();
    }
    await separator.focus();
    await page.keyboard.press('ArrowRight'); await value(mobile ? 41 : 1);
    await page.getByRole('button', {name: 'Reset panes', exact: true}).click(); await value(40);
    assert.equal(await primary.inputValue(), 'retained');
    assert.equal(await secondary.inputValue(), 'preview');
    await page.getByRole('button', {name: 'Add', exact: true}).click();
    await page.waitForFunction(() => Module.textBlocks.some(block => block.text === 'Count: 1'));
    assert.deepEqual(errors, []);
    console.log(`web split: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkRangeBoundary(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: mobile ? 390 : 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.webFrame.items.filter(item => item.role === 8).length === 2);
    const full = page.getByRole('slider', {name: 'Full', exact: true});
    const negative = page.getByRole('slider', {name: 'Negative', exact: true});
    assert.equal(await full.getAttribute('aria-valuemin'), '-9223372036854775808');
    assert.equal(await full.getAttribute('aria-valuemax'), '9223372036854775807');
    assert.equal(await negative.getAttribute('aria-valuenow'), '-9223372036854775807');
    const expectValue = async (node, value) => {
      await page.waitForFunction(({node, value}) => node.getAttribute('aria-valuenow') === value &&
        Module.textBlocks.some(block => block.text === value), {node: await node.elementHandle(), value});
      assert.equal(await node.getAttribute('aria-valuetext'), value);
    };
    const point = async (label, fraction) => page.evaluate(({label, fraction}) => {
      const item = Module.webFrame.items.find(item => item.role === 8 && item.label === label);
      const scale = Module.webFrame.scale;
      const y = (item.y + item.h / 2) / scale;
      for (const [phase, position] of [[1, .5], [2, fraction], [3, fraction]])
        Module.ccall('sz_web_pointer', null, ['number', 'number', 'number'], [(item.x + item.w * position) / scale, y, phase]);
    }, {label, fraction});
    for (const [fraction, value] of [[0, '-9223372036854775808'], [.25, '-4611686018427387904'],
      [.5, '0'], [.75, '4611686018427387903'], [1, '9223372036854775807']]) {
      await point('Full', fraction);
      await expectValue(full, value);
    }
    await full.focus();
    await page.keyboard.press('ArrowLeft');
    await expectValue(full, '9223372036854775806');
    await page.keyboard.press('Home');
    await expectValue(full, '-9223372036854775808');
    await page.keyboard.press('ArrowRight');
    await expectValue(full, '-9223372036854775807');
    await point('Negative', .5);
    await expectValue(negative, '-9223372036854775803');
    await negative.focus();
    await page.keyboard.press('ArrowRight');
    await expectValue(negative, '-9223372036854775802');
    await page.keyboard.press('PageUp');
    await expectValue(negative, '-9223372036854775798');
    await page.keyboard.press('PageDown');
    await expectValue(negative, '-9223372036854775808');
    assert.deepEqual(errors, []);
    console.log(`web range boundary: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkProgress(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 640, height: 1400},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.nodes.size > 0);
    const linear = page.getByRole('progressbar', {name: 'Linear completion', exact: true});
    const circular = page.getByRole('progressbar', {name: 'Circular completion', exact: true});
    await linear.waitFor();
    await circular.waitFor();
    const original = await linear.elementHandle();
    const expectValue = async value => {
      for (const node of [linear, circular]) {
        await page.waitForFunction(({node, value}) => node.getAttribute('aria-valuenow') === value,
          {node: await node.elementHandle(), value});
        assert.equal(await node.getAttribute('aria-valuemin'), '0');
        assert.equal(await node.getAttribute('aria-valuemax'), '100');
        assert.equal(await node.getAttribute('tabindex'), '-1');
        assert.equal(await node.getAttribute('aria-valuetext'), null);
      }
    };
    await expectValue('50');
    assert.equal(await page.getByRole('progressbar').count(), 2);
    const volume = page.getByRole('slider', {name: 'Volume', exact: true});
    await volume.focus();
    for (const [key, value] of [['Home', '0'], ['PageUp', '0'], ['End', '100'],
      ['PageDown', '100'], ['Home', '0']]) {
      await page.keyboard.press(key);
      await expectValue(value);
    }
    await page.getByRole('button', {name: 'Mute', exact: true}).click();
    await page.waitForFunction(() => Module.ccall('sz_web_snapshot', 'string', [], []).includes('slider:Volume=0'));
    await expectValue('0');
    assert(await original.evaluate(node => node === document.querySelector('[role="progressbar"][aria-label="Linear completion"]')));
    const box = await linear.boundingBox();
    await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
    assert.equal(await linear.evaluate(node => node === document.activeElement), false);
    await expectValue('0');
    await page.getByRole('checkbox', {name: 'Updates', exact: true}).click();
    await page.getByRole('progressbar', {name: 'Optional completion', exact: true}).waitFor();
    assert.equal(await page.getByRole('progressbar').count(), 3);
    assert.equal(await page.locator('[aria-label="Private completion"]').count(), 0);
    await page.getByRole('button', {name: 'Open', exact: true}).click();
    await page.getByRole('dialog', {name: 'Edit details', exact: true}).waitFor();
    assert.equal(await page.getByRole('progressbar').count(), 0);
    await page.keyboard.press('Escape');
    await linear.waitFor();
    await expectValue('0');
    await page.evaluate(() => {
      const outside = document.createElement('button');
      outside.textContent = 'Outside';
      document.body.append(outside);
    });
    await page.getByRole('slider', {name: 'Precise count', exact: true}).focus();
    await page.keyboard.press('Tab');
    assert.equal(await page.evaluate(() => document.activeElement.getAttribute('aria-label')), 'Mute');
    await page.keyboard.press('Tab');
    assert.equal(await page.evaluate(() => document.activeElement.textContent), 'Outside');
    assert.deepEqual(errors, []);
    console.log(`web progress: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function checkControls(browserType, url, mobile) {
  const browser = await browserType.launch({headless: true});
  try {
    const context = await browser.newContext({viewport: {width: 640, height: 720},
      deviceScaleFactor: mobile ? 2 : 1, hasTouch: mobile, isMobile: mobile});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.stack));
    await page.goto(url);
    await page.waitForFunction(() => Module.ready && Module.nodes.size > 0);
    await page.evaluate(() => {
      const outside = document.createElement('button');
      outside.textContent = 'Outside';
      outside.style.cssText = 'position:fixed;right:0;bottom:0;width:1px;height:1px';
      document.body.append(outside);
    });
    await page.evaluate(() => Module.canvas.focus());
    const activeLabel = () => page.evaluate(() => document.activeElement.getAttribute('aria-label'));
    const settle = () => page.evaluate(() => new Promise(resolve => {
      let left = 12;
      const frame = () => --left ? requestAnimationFrame(frame) : resolve();
      requestAnimationFrame(frame);
    }));
    const canvasTap = (x, y) => page.evaluate(({x, y}) => {
      for (const phase of [1, 3]) Module.ccall('sz_web_pointer', null, ['number', 'number', 'number'], [x, y, phase]);
    }, {x, y});

    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Name');
    await page.keyboard.type('Ada');
    await page.waitForFunction(() => Module.ccall('sz_web_snapshot', 'string', [], [])
      .includes('text:Name: Ada'));
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Updates');
    await page.keyboard.press('Space');
    await page.waitForFunction(() => document.activeElement.getAttribute('aria-checked') === 'true');
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Volume');
    assert.equal(await page.getByRole('slider', {name: 'Volume', exact: true}).getAttribute('aria-valuenow'), '50');
    await page.keyboard.press('ArrowRight');
    await page.waitForFunction(() => Module.ccall('sz_web_snapshot', 'string', [], [])
      .includes('text:Volume: 51'));
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Basic');
    assert.equal(await page.getByRole('radiogroup', {name: 'Plan', exact: true}).count(), 1);
    await page.keyboard.press('ArrowRight');
    assert.equal(await activeLabel(), 'Pro');
    await page.waitForFunction(() => document.activeElement.getAttribute('aria-checked') === 'true');
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'editor');
    await page.keyboard.press('Control+Shift+Tab');
    assert.equal(await activeLabel(), 'Pro');
    await page.waitForFunction(() => document.querySelector('[role=radio][aria-label=Pro]').tabIndex === 0 &&
      document.querySelector('[role=radio][aria-label=Basic]').tabIndex === -1);
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'editor');
    const editor = page.getByRole('textbox', {name: 'editor', exact: true});
    await editor.fill('λx');
    await editor.evaluate(node => { node.setSelectionRange(1, 2); node.dispatchEvent(new Event('select')); });
    await page.keyboard.press('Tab');
    assert.equal(await editor.inputValue(), 'λ  ');
    assert.deepEqual(await editor.evaluate(node => [node.selectionStart, node.selectionEnd]), [3, 3]);
    await page.keyboard.press('Control+Tab');
    assert.equal(await activeLabel(), 'Submit');
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => Module.ccall('sz_web_snapshot', 'string', [], [])
      .includes('text:Submitted'));
    assert.equal(await page.getByRole('textbox', {name: 'Name', exact: true}).inputValue(), 'Ada');
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Open');
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Temperature (°C)');
    const temperature = page.getByRole('slider', {name: 'Temperature (°C)', exact: true});
    const precise = page.getByRole('slider', {name: 'Precise count', exact: true});
    assert.equal(await temperature.getAttribute('aria-valuemin'), '-60');
    assert.equal(await temperature.getAttribute('aria-valuemax'), '120');
    const valueAfter = async (node, key, value) => {
      await page.keyboard.press(key);
      await page.waitForFunction(({node, value}) => node.getAttribute('aria-valuenow') === value,
        {node: await node.elementHandle(), value});
      assert.equal(await node.getAttribute('aria-valuetext'), value);
    };
    await valueAfter(temperature, 'ArrowUp', '-12');
    await valueAfter(temperature, 'PageUp', '18');
    await valueAfter(temperature, 'PageDown', '-12');
    await valueAfter(temperature, 'Home', '-60');
    await valueAfter(temperature, 'End', '120');
    let rangeBox = await temperature.boundingBox();
    if (mobile) await page.touchscreen.tap(rangeBox.x + rangeBox.width / 2, rangeBox.y + rangeBox.height / 2);
    else {
      await page.mouse.move(rangeBox.x + rangeBox.width / 4, rangeBox.y + rangeBox.height / 2);
      await page.mouse.down();
      await page.mouse.move(rangeBox.x + rangeBox.width * 3 / 4, rangeBox.y + rangeBox.height / 2);
      await page.mouse.up();
    }
    await page.waitForFunction(({node, value}) => node.getAttribute('aria-valuenow') === value,
      {node: await temperature.elementHandle(), value: mobile ? '30' : '75'});
    await valueAfter(temperature, 'ArrowRight', mobile ? '33' : '78');
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Precise count');
    assert.equal(await precise.getAttribute('aria-valuemin'), '9223372036854775797');
    assert.equal(await precise.getAttribute('aria-valuemax'), '9223372036854775807');
    await valueAfter(precise, 'ArrowRight', '9223372036854775807');
    await valueAfter(precise, 'ArrowRight', '9223372036854775807');
    await valueAfter(precise, 'PageDown', '9223372036854775797');
    await valueAfter(precise, 'ArrowRight', '9223372036854775798');
    await valueAfter(precise, 'PageUp', '9223372036854775807');
    await valueAfter(precise, 'Home', '9223372036854775797');
    await valueAfter(precise, 'End', '9223372036854775807');
    rangeBox = await precise.boundingBox();
    if (mobile) {
      await page.touchscreen.tap(rangeBox.x + rangeBox.width / 2, rangeBox.y + rangeBox.height / 2);
      await page.waitForFunction(node => node.getAttribute('aria-valuenow') === '9223372036854775802', await precise.elementHandle());
    } else {
      await page.mouse.move(rangeBox.x + rangeBox.width / 10, rangeBox.y + rangeBox.height / 2);
      await page.mouse.down();
      await page.mouse.move(rangeBox.x + rangeBox.width + 8, rangeBox.y + rangeBox.height / 2);
      await page.mouse.up();
      await page.waitForFunction(node => node.getAttribute('aria-valuenow') === '9223372036854775807', await precise.elementHandle());
    }
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Mute');
    await page.keyboard.press('Tab');
    assert.equal(await page.evaluate(() => document.getElementById('app').contains(document.activeElement)), false);
    await page.keyboard.press('Shift+Tab');
    assert.equal(await activeLabel(), 'Mute');
    await page.keyboard.press('Shift+Tab');
    assert.equal(await activeLabel(), 'Precise count');
    await page.keyboard.press('Shift+Tab');
    assert.equal(await activeLabel(), 'Temperature (°C)');
    await page.keyboard.press('Shift+Tab');
    assert.equal(await activeLabel(), 'Open');
    await page.keyboard.press('Enter');
    const dialog = page.getByRole('dialog', {name: 'Edit details', exact: true});
    await dialog.waitFor();
    assert.equal(await dialog.getAttribute('aria-modal'), 'true');
    await page.waitForFunction(() => document.activeElement.getAttribute('aria-label') === 'Dialog');
    assert.equal(await page.getByRole('textbox', {name: 'Name', exact: true}).count(), 0);
    assert.equal(await page.getByRole('button', {name: 'Submit', exact: true}).count(), 0);
    assert.equal(await dialog.getByRole('heading', {name: 'Edit details', exact: true}).count(), 1);
    assert((await dialog.textContent()).includes('Enter a name, then review the details.'));
    const heading = await dialog.getByRole('heading').locator('span').first().boundingBox();
    await canvasTap(heading.x + 10, heading.y + heading.height / 2);
    const fieldBox = await page.getByRole('textbox', {name: 'Dialog', exact: true}).boundingBox();
    await canvasTap(fieldBox.x - 8, fieldBox.y - 8);
    await page.mouse.move(30, 30);
    await settle();
    const beforeWheel = await page.evaluate(() => Module.canvas.toDataURL());
    if (mobile) await page.evaluate(() => Module.ccall('sz_web_scroll', null,
      ['number', 'number', 'number', 'number'], [30, 30, 0, 120]));
    else await page.mouse.wheel(0, 120);
    await settle();
    assert.equal(await page.evaluate(() => Module.canvas.toDataURL()), beforeWheel);
    await page.evaluate(() => {
      for (const [phase, y] of [[1, 400], [2, 300], [3, 300]])
        Module.ccall('sz_web_pointer', null, ['number', 'number', 'number'], [30, y, phase]);
    });
    await settle();
    assert.equal(await page.evaluate(() => Module.canvas.toDataURL()), beforeWheel);
    assert.equal(await dialog.count(), 1);
    await page.getByRole('textbox', {name: 'Dialog', exact: true}).fill('Retained');
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Review details');
    await page.keyboard.press('Enter');
    await page.getByRole('dialog', {name: 'Confirm details', exact: true}).waitFor();
    await page.waitForFunction(() => document.activeElement.getAttribute('aria-label') === 'Back');
    assert.equal(await dialog.count(), 0);
    await page.evaluate(() => {
      const line = Module.textBlocks.find(block => block.text.startsWith('Check the name')).lines[0];
      const scale = Module.webFrame.scale;
      for (const phase of [1, 3]) Module.ccall('sz_web_pointer', null, ['number', 'number', 'number'],
        [(line.x + 10) / scale, (line.y - line.font / 2) / scale, phase]);
    });
    await settle();
    assert.equal(await page.getByRole('dialog', {name: 'Confirm details', exact: true}).count(), 1);

    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Back');
    await page.keyboard.press('Escape');
    await dialog.waitFor();
    await page.waitForFunction(() => document.activeElement.getAttribute('aria-label') === 'Review details');
    assert.equal(await page.getByRole('textbox', {name: 'Dialog', exact: true}).inputValue(), 'Retained');
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Close');
    await page.keyboard.press('Tab');
    assert.equal(await activeLabel(), 'Dialog');
    await page.keyboard.press('Shift+Tab');
    assert.equal(await activeLabel(), 'Close');
    await page.keyboard.press('Escape');
    await page.waitForFunction(() => document.activeElement.getAttribute('aria-label') === 'Open');
    assert.equal(await page.getByRole('dialog').count(), 0);
    assert.equal(await page.getByRole('textbox', {name: 'Name', exact: true}).inputValue(), 'Ada');
    await page.keyboard.press('Enter');
    await dialog.waitFor();
    await page.getByRole('button', {name: 'Close', exact: true}).click();
    await page.waitForFunction(() => document.activeElement.getAttribute('aria-label') === 'Open');
    await page.keyboard.press('Enter');
    await dialog.waitFor();
    await page.mouse.click(30, 30);
    await page.waitForFunction(() => document.activeElement.getAttribute('aria-label') === 'Open');
    assert.equal(await page.getByRole('dialog').count(), 0);
    assert.deepEqual(errors, []);
    console.log(`web controls: ${browserType.name()} ${mobile ? 'mobile emulation' : 'desktop'} passed`);
  } finally { await browser.close(); }
}

async function main() {
  const directory = path.resolve(process.argv[2]);
  for (const file of ['index.html', 'app.js', 'app.wasm']) assert(fs.existsSync(path.join(directory, file)), `missing web asset: ${file}`);
  assert.equal(fs.readFileSync(path.join(directory, 'SheenBidi-LICENSE'), 'utf8'),
    fs.readFileSync(path.resolve(__dirname, '../../third_party/skia/SheenBidi-LICENSE'), 'utf8'), 'browser direction license');
  assert.equal(fs.readFileSync(path.join(directory, 'HarfBuzz-LICENSE'), 'utf8'),
    fs.readFileSync(path.resolve(__dirname, '../ffi-skia/build/harfbuzz/HarfBuzz-LICENSE'), 'utf8'), 'browser joining license');
  const html = fs.readFileSync(path.join(directory, 'index.html'), 'utf8');
  const js = fs.readFileSync(path.join(directory, 'app.js'), 'utf8');
  const version = html.match(/src="\.\/app\.js\?v=([0-9a-f]{16})"/);
  assert(version, 'app.js cache version');
  assert(js.includes(`locateFile("app.wasm?v=${version[1]}")`), 'wasm cache version');
  const server = http.createServer((request, response) => {
    const pathname = new URL(request.url, 'http://localhost').pathname;
    const relative = pathname === '/scuzz/' ? 'index.html' : pathname.replace(/^\/scuzz\//, '');
    if (!['index.html', 'app.js', 'app.wasm', 'fonts/fonts.css'].includes(relative) && !/^fonts\/(arabic|devanagari|thai|sc)-[0-9a-f]{16}\.ttf$/.test(relative)) { response.writeHead(404).end(); return; }
    if (!fs.existsSync(path.join(directory, relative))) { response.writeHead(404).end(); return; }
    response.setHeader('Content-Type', relative.endsWith('.wasm') ? 'application/wasm' : relative.endsWith('.js') ? 'text/javascript' : relative.endsWith('.css') ? 'text/css' : relative.endsWith('.ttf') ? 'font/ttf' : 'text/html');
    response.end(fs.readFileSync(path.join(directory, relative)));
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const url = `http://127.0.0.1:${server.address().port}/scuzz/`;
    for (const type of [chromium, firefox, webkit]) {
      if (process.argv[3] && process.argv[3] !== type.name()) continue;
      const run = process.argv[4] === 'scale-limit' ? checkScaleLimit : process.argv[4] === 'scale-input' ? checkScaleInput : process.argv[4] === 'scale-motion' ? checkScaleMotion : process.argv[4] === 'position-limit' ? checkPositionLimit : process.argv[4] === 'position-motion' ? checkPositionMotion : process.argv[4] === 'content-motion' ? checkContentMotion : process.argv[4] === 'size-limit' ? checkSizeLimit : process.argv[4] === 'size-motion' ? checkSizeMotion : process.argv[4] === 'progress' ? checkProgress : process.argv[4] === 'motion-unicode' ? checkMotionUnicode : process.argv[4] === 'motion' ? checkMotion : process.argv[4] === 'select' ? checkSelect : process.argv[4] === 'shaping' ? checkShaping : process.argv[4] === 'tooltips' ? checkTooltips : process.argv[4] === 'inline' ? checkInline : process.argv[4] === 'disclosure' ? checkDisclosure : process.argv[4] === 'choices' ? checkChoices : process.argv[4] === 'split' ? checkSplit : process.argv[4] === 'range-boundary' ? checkRangeBoundary : process.argv[4] === 'text' ? checkText : process.argv[4] === 'measured' ? checkMeasured : process.argv[4] === 'themes' ? checkThemes : process.argv[4] === 'layout' ? checkLayout : process.argv[4] === 'list' ? checkList : process.argv[4] === 'controls' ? checkControls : check;
      await run(type, url, false);
      if (type !== firefox) await run(type, url, true);
    }
  } finally { await new Promise(resolve => server.close(resolve)); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
