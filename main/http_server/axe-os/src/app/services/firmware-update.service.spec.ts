import { TestBed } from '@angular/core/testing';

import { FirmwareUpdateService } from './firmware-update.service';
import { provideHttpClient } from '@angular/common/http';
import { firmwareFolderUrl, publishedFirmware, readPublishedVersion } from './firmware-update-source';

describe('FirmwareUpdateService', () => {
  let service: FirmwareUpdateService;

  beforeEach(() => {
    TestBed.configureTestingModule({
      providers: [provideHttpClient()]
    });
    service = TestBed.inject(FirmwareUpdateService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  it('reads version.txt and the two files in that folder', () => {
    const published = publishedFirmware(
      'https://updates.example/firmware/',
      'split-p6\n'
    );

    expect(published.version).toBe('split-p6');
    expect(published.firmwareUrl).toBe('https://updates.example/firmware/esp-miner.bin');
    expect(published.wwwUrl).toBe('https://updates.example/firmware/www.bin');
    expect(readPublishedVersion('  split-p6  ')).toBe('split-p6');
    expect(firmwareFolderUrl('https://updates.example/firmware/')).toBe('https://updates.example/firmware');
  });

  it('rejects a folder path that is not a web address', () => {
    expect(() => firmwareFolderUrl('firmware')).toThrow();
  });
});
