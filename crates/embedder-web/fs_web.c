/* Browser files. Keep the working directory across a page reload.
 * Load it before a file call. Save it after a successful change.
 * Stay on this thread. Do not start a worker. */
#include "scuzz_rt.h"

#include <emscripten.h>

static int ready;

EM_JS(int, web_fs_mount, (), {
  try {
    if (!FS.analyzePath('/scuzz').exists) FS.mkdir('/scuzz');
    if (!Module.scuzzFs) {
      FS.mount(IDBFS, {}, '/scuzz');
      Module.scuzzFs = 1;
    }
    FS.chdir('/scuzz');
    return 0;
  } catch (error) {
    console.error(error);
    return 1;
  }
});

EM_ASYNC_JS(int, web_fs_sync, (int populate), {
  try {
    await new Promise((resolve, reject) => {
      FS.syncfs(Boolean(populate), (err) => {
        if (err) reject(err);
        else resolve();
      });
    });
    return 0;
  } catch (error) {
    console.error(error);
    return 1;
  }
});

int sz_web_fs_prepare(void) {
  if (ready)
    return 0;
  if (web_fs_mount() != 0)
    return 1;
  if (web_fs_sync(1) != 0)
    return 1;
  ready = 1;
  return 0;
}

int sz_web_fs_flush(void) {
  if (!ready)
    return 1;
  return web_fs_sync(0);
}
