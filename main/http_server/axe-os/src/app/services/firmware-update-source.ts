export interface PublishedFirmware {
  version: string;
  wwwUrl: string;
  firmwareUrl: string;
}

/** The update folder address comes from the miner. It must be an http(s) URL with no trailing slash. */
export function firmwareFolderUrl(base: string): string {
  const folder = String(base ?? '').trim().replace(/\/+$/, '');
  if (!/^https?:\/\/.+/i.test(folder)) {
    throw new Error('The update server path is not set.');
  }
  return folder;
}

export function readPublishedVersion(versionText: string): string {
  const version = versionText.replace(/^\uFEFF/, '').trim().split(/\r?\n/, 1)[0].trim();
  if (!version) {
    throw new Error('The Satoshiware OS server did not return a version.');
  }
  return version;
}

export function publishedFirmware(base: string, versionText: string): PublishedFirmware {
  const folder = firmwareFolderUrl(base);
  return {
    version: readPublishedVersion(versionText),
    wwwUrl: `${folder}/www.bin`,
    firmwareUrl: `${folder}/esp-miner.bin`,
  };
}
