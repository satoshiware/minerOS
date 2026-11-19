import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable, forkJoin } from 'rxjs';
import { map } from 'rxjs/operators';
import {
  FirmwareUpdateSource,
  PublishedFirmware,
  readPublishedFirmware,
} from './firmware-update-source';

interface GithubContentEntry {
  name?: string;
  size?: number;
  sha?: string;
  download_url?: string | null;
}

@Injectable({
  providedIn: 'root'
})
export class GithubUpdateService {

  constructor(
    private httpClient: HttpClient
  ) { }

  /** Read version.txt plus esp-miner.bin and www.bin from one public GitHub folder. */
  public check(source: FirmwareUpdateSource): Observable<PublishedFirmware> {
    const owner = encodeURIComponent(source.owner);
    const repo = encodeURIComponent(source.repo);
    const branch = encodeURIComponent(source.branch);
    const folder = source.folder.split('/').map(encodeURIComponent).join('/');
    const contentsUrl = `https://api.github.com/repos/${owner}/${repo}/contents/${folder}?ref=${branch}`;
    const versionUrl = `https://raw.githubusercontent.com/${owner}/${repo}/${branch}/version.txt`;

    return forkJoin({
      entries: this.httpClient.get<GithubContentEntry[]>(contentsUrl),
      version: this.httpClient.get(versionUrl, { responseType: 'text' }),
    }).pipe(
      map(({ entries, version }) => readPublishedFirmware(Array.isArray(entries) ? entries : [], version))
    );
  }

  public download(url: string): Observable<Blob> {
    return this.httpClient.get(url, { responseType: 'blob' });
  }
}
