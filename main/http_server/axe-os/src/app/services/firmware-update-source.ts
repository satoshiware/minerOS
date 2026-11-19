export interface FirmwareUpdateSource {
  owner: string;
  repo: string;
  branch: string;
  folder: string;
}

/** Published bins the install commands already download. Change this if that folder moves. */
export const DEFAULT_FIRMWARE_UPDATE_SOURCE: FirmwareUpdateSource = {
  owner: 'satoshiware',
  repo: 'minerOS',
  branch: 'main',
  folder: 'firmware',
};

export const FIRMWARE_UPDATE_SOURCE_KEY = 'FIRMWARE_UPDATE_SOURCE';

const NAME = /^[A-Za-z0-9_.-]+$/;
const FOLDER = /^[A-Za-z0-9_./-]+$/;

export function firmwareUpdateSourceError(source: FirmwareUpdateSource): string | null {
  if (!NAME.test(source.owner) || !NAME.test(source.repo) || !NAME.test(source.branch)) {
    return 'Owner, repository, and branch use letters, numbers, dots, dashes, and underscores.';
  }
  if (!FOLDER.test(source.folder) || source.folder.includes('..') || source.folder.startsWith('/') || source.folder.endsWith('/')) {
    return 'Folder is a path like firmware, with no leading or trailing slash.';
  }
  return null;
}

export function normalizeFirmwareUpdateSource(value: unknown): FirmwareUpdateSource {
  if (!value || typeof value !== 'object') {
    return { ...DEFAULT_FIRMWARE_UPDATE_SOURCE };
  }
  const raw = value as Partial<FirmwareUpdateSource>;
  const source: FirmwareUpdateSource = {
    owner: String(raw.owner ?? '').trim(),
    repo: String(raw.repo ?? '').trim(),
    branch: String(raw.branch ?? '').trim(),
    folder: String(raw.folder ?? '').trim(),
  };
  if (firmwareUpdateSourceError(source)) {
    return { ...DEFAULT_FIRMWARE_UPDATE_SOURCE };
  }
  return source;
}

export interface PublishedBin {
  name: 'esp-miner.bin' | 'www.bin';
  size: number;
  sha: string;
  downloadUrl: string;
}

export interface PublishedFirmware {
  version: string;
  espMiner: PublishedBin;
  www: PublishedBin;
}

interface GithubContentEntry {
  name?: string;
  size?: number;
  sha?: string;
  download_url?: string | null;
}

export function readPublishedFirmware(entries: GithubContentEntry[], versionText: string): PublishedFirmware {
  const version = versionText.replace(/^\uFEFF/, '').trim();
  const espMiner = bin(entries, 'esp-miner.bin');
  const www = bin(entries, 'www.bin');
  if (!version || !espMiner || !www) {
    throw new Error('That folder needs esp-miner.bin, www.bin, and version.txt on the same branch.');
  }
  return { version, espMiner, www };
}

function bin(entries: GithubContentEntry[], name: PublishedBin['name']): PublishedBin | null {
  const entry = entries.find((item) => item?.name === name && !!item.download_url);
  if (!entry?.download_url || !entry.sha || typeof entry.size !== 'number') {
    return null;
  }
  return { name, size: entry.size, sha: entry.sha, downloadUrl: entry.download_url };
}
