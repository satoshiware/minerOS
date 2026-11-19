import { TestBed } from '@angular/core/testing';

import { GithubUpdateService } from './github-update.service';
import { provideHttpClient } from '@angular/common/http';
import {
  firmwareUpdateSourceError,
  normalizeFirmwareUpdateSource,
  readPublishedFirmware,
} from './firmware-update-source';

describe('GithubUpdateService', () => {
  let service: GithubUpdateService;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [provideHttpClient()]
    });
    service = TestBed.inject(GithubUpdateService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  it('reads the two published bins and the branch version', () => {
    const published = readPublishedFirmware([
      { name: 'esp-miner.bin', size: 1742624, sha: 'abc', download_url: 'https://example.com/esp-miner.bin' },
      { name: 'www.bin', size: 800000, sha: 'def', download_url: 'https://example.com/www.bin' },
      { name: 'notes.txt', size: 12, sha: 'ghi', download_url: 'https://example.com/notes.txt' },
    ], 'split-p6\n');

    expect(published.version).toBe('split-p6');
    expect(published.espMiner.size).toBe(1742624);
    expect(published.www.downloadUrl).toBe('https://example.com/www.bin');
  });

  it('rejects a folder that is missing a bin', () => {
    expect(() => readPublishedFirmware([
      { name: 'www.bin', size: 1, sha: 'def', download_url: 'https://example.com/www.bin' },
    ], 'split-p6')).toThrow();
  });

  it('keeps the default source when a saved value is not a repo path', () => {
    expect(normalizeFirmwareUpdateSource({ owner: '../x', repo: 'minerOS', branch: 'main', folder: 'firmware' }).owner)
      .toBe('satoshiware');
    expect(firmwareUpdateSourceError({ owner: 'satoshiware', repo: 'minerOS', branch: 'main', folder: 'firmware' }))
      .toBeNull();
  });
});
